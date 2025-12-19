#pragma once

#include <absl/container/flat_hash_map.h>
#include <absl/hash/hash.h>
#include <boost/flyweight.hpp>
#include <glaze/glaze.hpp>
#include <tsl/sparse_map.h>
#include <string>
#include <vector>
#include <variant>
#include <optional>
#include <memory>
#include <array>
#include <bit>
#include <unordered_map>

namespace json
{
    using AttributeValue =
    std::variant<bool, double, std::string, std::vector<glz::raw_json>, std::optional<glz::raw_json>>;

    struct Attribute
    {
        std::string id{};
        AttributeValue value{};
    };

    struct Item
    {
        std::string id{};
        std::vector<Attribute> attributes{};
    };

    struct Pagination
    {
        int page{};
        int pages{};
    };

    struct Result
    {
        int count{};
        Pagination pagination{};
        std::vector<Item> data{};
    };

    struct Response
    {
        Result result{};
    };
} // namespace json

class SmallCache
{
public:
    using str = std::string;
    using strVec = std::vector<str>;
    using fwStr = boost::flyweight<str>;
    using strVecUPtr = std::unique_ptr<std::vector<fwStr>>;

    using AttributeValue = std::variant<std::monostate, double, bool, fwStr, strVecUPtr>;
    using pyAttrValue = std::variant<std::monostate, bool, double, str, strVec>;

    explicit SmallCache(const strVec& attributes);
    SmallCache(const SmallCache&) = delete;
    SmallCache& operator=(const SmallCache&) = delete;

    struct MarkedItem
    {
        bool isNew = true;
        std::array<uint32_t, 3> attrs_flags{}; // 96 bits total
        static constexpr std::size_t maxAttributes =
            std::tuple_size_v<decltype(attrs_flags)> *
            std::numeric_limits<std::remove_reference_t<decltype(attrs_flags)>::value_type>::digits;
        std::vector<AttributeValue> value;

        [[nodiscard]] std::vector<size_t> getIdxs() const;

        [[nodiscard]] constexpr bool hasIdx(size_t idx) const noexcept
        {
            if (idx >= attrs_flags.size() * 32)
                return false; // out of range
            auto w = idx / 32;
            auto b = idx % 32;
            return (attrs_flags[w] >> b) & 1u;
        }

        [[nodiscard]] std::optional<std::reference_wrapper<AttributeValue>> getValue(size_t idx) noexcept;
        [[nodiscard]] std::optional<std::reference_wrapper<const AttributeValue>> getValue(size_t idx) const noexcept;
    };

    void add_item(const str& item_id, const std::unordered_map<str, pyAttrValue>& attributes);
    std::vector<pyAttrValue> get_one(const str& id, const strVec& attributes);
    std::vector<std::vector<pyAttrValue>> get_many(const strVec& ids, const strVec& attributes);
    std::vector<str> get_all_ids();
    void begin_transaction(uint64_t estimated_number_of_items = 0, bool remove_old_items = true);
    void end_transaction();
    size_t load_page(const str& json_text);
    void print_variant_stats() const;

    static str to_string(const pyAttrValue& src);

private:
    void setMarkedItem(MarkedItem& item, const std::unordered_map<str, pyAttrValue>& attrs);
    static pyAttrValue convert_valueJ(const json::AttributeValue& src);
    static AttributeValue convert_value(const json::AttributeValue& src);
    static pyAttrValue convert_value(const AttributeValue& src);
    static AttributeValue convert_value(const pyAttrValue& src);

public:
    tsl::sparse_map<str, MarkedItem> cache;
    absl::flat_hash_map<str, uint8_t, absl::Hash<str>> attrMap;
    strVec attrIdx;
    const uint8_t numberOfAttributes;
    size_t oldCacheSize = 0;
    bool transactionOpened = false;
    bool transactionShouldRemoveOldItems = true;
};
