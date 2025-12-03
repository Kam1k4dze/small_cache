#include <nanobind/nanobind.h>
#include <nanobind/stl/variant.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/vector.h>
#include <nanobind/stl/pair.h>
#include <nanobind/stl/optional.h>
#include <nanobind/stl/unordered_map.h>
#include <tsl/sparse_map.h>
#include <absl/hash/hash.h>
#include <absl/container/flat_hash_map.h>
#include <absl/container/flat_hash_set.h>
#include <boost/flyweight.hpp>
#include <optional>
#include <algorithm>
#include <variant>
#include <vector>
#include <string>
#include <glaze/glaze.hpp>

namespace json {
    using AttributeValue = std::variant<bool, double, std::string,
        std::vector<glz::raw_json>,
        std::optional<glz::raw_json> >;

    struct Attribute {
        std::string id{};
        AttributeValue value{};
    };

    struct Item {
        std::string id{};
        std::vector<Attribute> attributes{};
    };

    struct Pagination {
        int page{};
        int pages{};
    };

    struct Result {
        int count{};
        Pagination pagination{};
        std::vector<Item> data{};
    };

    struct Response {
        Result result{};
    };
}


namespace nb = nanobind;
using namespace nb::literals;

class SmallCache {
    using str = std::string;
    using strVec = std::vector<str>;
    using fwStr = boost::flyweight<str>;
    using strVecUPtr = std::unique_ptr<std::vector<fwStr>>;

    using AttributeValue = std::variant<std::monostate, double, bool, fwStr, strVecUPtr>;
    using pyAttrValue = std::variant<std::monostate, bool, double, str, strVec>;

public:
    explicit SmallCache(const strVec &attributes) : numberOfAttributes(attributes.size()) {
        if (attributes.empty()) {
            throw std::runtime_error("No attributes provided");
        }
        if (attributes.size() > 255) {
            throw std::runtime_error("Too many attributes provided");
        }
        attrMap.reserve(attributes.size());
        attrIdx.reserve(attributes.size());
        for (size_t idx = 0; idx < attributes.size(); ++idx) {
            const auto &attr = attributes[idx];
            attrIdx.emplace_back(attr);
            attrMap.emplace(attr, idx);
        }
    }

    SmallCache(const SmallCache &) = delete;

    SmallCache &operator=(const SmallCache &) = delete;

    struct MarkedItem {
        bool isNew = true;
        std::array<uint32_t, 3> attrs_flags{}; // 96 bits total
        std::vector<AttributeValue> value;

        [[nodiscard]] std::vector<size_t> getIdxs() const {
            std::vector<size_t> idxs;
            idxs.reserve(value.size());

            for (size_t w = 0; w < attrs_flags.size(); ++w) {
                uint32_t bits = attrs_flags[w];
                while (bits) {
                    unsigned b = std::countr_zero(bits);
                    idxs.push_back(w * 32 + b);
                    bits &= bits - 1; // clear that bit
                }
            }
            return idxs;
        }

        [[nodiscard]] constexpr bool hasIdx(size_t idx) const noexcept {
            if (idx >= attrs_flags.size() * 32)
                return false; // out of range
            auto w = idx / 32;
            auto b = idx % 32;
            return (attrs_flags[w] >> b) & 1u;
        }

        [[nodiscard]] std::optional<std::reference_wrapper<AttributeValue>> getValue(size_t idx) noexcept {
            if (!hasIdx(idx))
                return std::nullopt;

            // count how many bits are set before 'idx'
            size_t w = idx / 32, b = idx % 32;
            size_t pos = 0;

            // sum full words
            for (size_t i = 0; i < w; ++i) {
                pos += std::popcount(attrs_flags[i]);
            }
            // sum lower bits in the same word
            if (b > 0) {
                uint32_t mask = (1u << b) - 1;
                pos += std::popcount(attrs_flags[w] & mask);
            }

            return value.size() > pos ? std::optional{std::ref(value[pos])} : std::nullopt; // just-in-case
        }

        [[nodiscard]] std::optional<std::reference_wrapper<const AttributeValue>> getValue(size_t idx) const noexcept {
            if (auto ref = const_cast<MarkedItem *>(this)->getValue(idx))
                return std::cref(ref->get());
            return std::nullopt;
        }
    };

    void setMarkedItem(MarkedItem &item, const std::unordered_map<str, pyAttrValue> &attrs) {
        item.isNew = true;
        item.attrs_flags.fill(0);

        // build a slot for each possible attr index
        std::vector<std::optional<AttributeValue>> slots(attrMap.size());

        // 1) collect into slots[] by index
        for (auto &[name, pyVal]: attrs) {
            if (auto it = attrMap.find(name); it != attrMap.end()) {
                slots[it->second] = convert_value(pyVal);
            }
        }

        // 2) reserve exactly as many as we’ll push
        auto count = std::ranges::count_if(slots, [](auto &o) { return o.has_value(); });
        item.value.clear();
        item.value.reserve(count);

        // 3) walk slots in ascending idx order,
        //    set flags and move values into item.value
        for (size_t idx = 0; idx < slots.size(); ++idx) {
            if (auto &opt = slots[idx]; opt) {
                item.value.push_back(std::move(*opt));
                auto w = idx / 32;
                auto b = idx % 32;
                item.attrs_flags[w] |= (1u << b);
            }
        }
    }


    struct getItemResponse {
        std::vector<pyAttrValue> attribute_values;
        std::vector<pyAttrValue> attribute_names;
    };


    template<class... Ts>
    struct overloaded : Ts... {
        using Ts::operator()...;
    };

    template<class... Ts>
    overloaded(Ts...) -> overloaded<Ts...>;

    static AttributeValue convert_value(const json::AttributeValue &src) {
        return std::visit(overloaded{
                                  [](bool b) -> AttributeValue { return b; },
                                  [](double d) -> AttributeValue { return d; },
                                  [](const str &s) -> AttributeValue {
                                      // if (s.empty()) return std::monostate{};
                                      return fwStr{s};
                                  },

                                  [](const std::vector<glz::raw_json> &json_vec) -> AttributeValue {
                                      auto out = std::make_unique<std::vector<fwStr>>();
                                      out->reserve(json_vec.size());
                                      for (auto &r: json_vec)
                                          if (!r.str.empty())
                                              out->emplace_back(r.str);
                                      // if (out->empty()) {
                                      //     return std::monostate{};
                                      // }
                                      out->shrink_to_fit();
                                      return out;
                                  },
                                  [](const std::optional<glz::raw_json> &o) -> AttributeValue {
                                      // if (!o.has_value() || o->str.empty()) return std::monostate{};
                                      return fwStr{o->str};
                                  },

                          },
                          src);
    }


    static pyAttrValue convert_valueJ(const json::AttributeValue &src) {
        return std::visit(overloaded{
                                  [](std::monostate b) -> pyAttrValue { return b; },
                                  [](bool b) -> pyAttrValue { return b; },
                                  [](double d) -> pyAttrValue { return d; },
                                  [](const str &s) -> pyAttrValue { return s; },
                                  [](const std::vector<glz::raw_json> &json_vec) -> pyAttrValue {
                                      return json_vec | std::views::transform([](const glz::raw_json &j) -> str {
                                                 return j.str;
                                             }) |
                                             std::ranges::to<strVec>();
                                  },
                                  [](const std::optional<glz::raw_json> &o) -> pyAttrValue { return o ? o->str : ""; },

                          },
                          src);
    }

    static pyAttrValue convert_value(const AttributeValue &src) {
        return std::visit(overloaded{
                                  [](std::monostate b) -> pyAttrValue { return b; },
                                  [](bool b) -> pyAttrValue { return b; },
                                  [](double d) -> pyAttrValue { return d; },
                                  [](const fwStr &s) -> pyAttrValue { return s; },
                                  [](const strVecUPtr &fw_vec) -> pyAttrValue {
                                      return *fw_vec | std::views::transform([](const fwStr &s) -> str { return s; }) |
                                             std::ranges::to<strVec>();
                                  },
                          },
                          src);
    }

    static AttributeValue convert_value(const pyAttrValue &src) {
        return std::visit(overloaded{
                                  [](std::monostate b) -> AttributeValue { return b; },
                                  [](bool b) -> AttributeValue { return b; },
                                  [](double d) -> AttributeValue { return d; },
                                  [](const str &s) -> AttributeValue { return fwStr{s}; },
                                  [](const strVec &vec) -> AttributeValue {
                                      auto out = std::make_unique<std::vector<fwStr>>();
                                      out->reserve(vec.size());
                                      for (auto &r: vec) {
                                          out->emplace_back(r);
                                      }
                                      out->shrink_to_fit();
                                      return out;
                                  },
                          },
                          src);
    }

    static str to_string(const pyAttrValue &src) {
        return std::visit(overloaded{
                                  [](std::monostate) -> str { return "null"; },
                                  [](bool b) -> str { return b ? "true" : "false"; },
                                  [](double d) -> str { return std::to_string(d); },
                                  [](const str &s) -> str { return s; },

                                  [](const strVec &vecptr) -> str {
                                      str out = "[";

                                      for (auto &r: vecptr) {
                                          out.append(r);
                                          out.append(",");
                                      }
                                      out.append("]");
                                      return out;
                                  },
                          },
                          src);
    }

    void add_item(const str &item_id, const std::unordered_map<str, pyAttrValue> &attributes) {
        if (!transactionOpened) {
            throw std::runtime_error("Transaction not opened");
        }
        auto &marked_attrs = cache[item_id];
        setMarkedItem(marked_attrs, attributes);
        increaseIteratorVersion();
    }

    std::vector<pyAttrValue> get_one(const str &id, const strVec &attributes) {
        if (attributes.empty()) {
            return {};
        }
        if (cache.contains(id)) {
            const auto &item = cache.at(id);
            return attributes | std::views::transform([this, &item](const auto &attr_name) -> pyAttrValue {
                       if (!attrMap.contains(attr_name))
                           // throw std::runtime_error("Attribute " + attr_name + " does not exist in cache");
                           return {};
                       const auto attr_idx = attrMap.at(attr_name);
                       const auto attr_value = item.getValue(attr_idx);
                       if (!attr_value)
                           return {};
                       return convert_value(*attr_value);
                   }) |
                   std::ranges::to<std::vector<pyAttrValue>>();
        }
        return {};
    }

    struct getManyResponse {
        uint64_t iterator_version{};
        uint64_t iterator{};
        std::vector<std::pair<str, std::vector<pyAttrValue>>> result{};
    };

    str get_many(const strVec &ids, const strVec &attributes, uint64_t per_iteration = 10000,
                 uint64_t iterator_version = 0, uint64_t iterator = 0) {
        if (transactionOpened)
            throw std::runtime_error("Can't get many when transaction is opened");
        if (iterator && iterator_version == 0) {
            throw std::runtime_error("Invalid iterator version");
        }
        if (iterator && iterator_version != this->iterator_version)
            throw std::runtime_error("Iterator already invalidated");
        getManyResponse resp{};
        std::vector<str> keys;
        if (ids.empty()) {
            keys = cache | std::views::keys | std::ranges::to<std::vector>();
            std::ranges::sort(keys);
        } else {
            absl::flat_hash_set<str> set_of_ids(ids.begin(), ids.end());
            keys = cache | std::views::keys |
                   std::views::filter([&](const auto &key) { return set_of_ids.contains(key); }) |
                   std::ranges::to<std::vector>();
        }
        std::ranges::sort(keys);

        const uint64_t start = iterator;
        const uint64_t available = keys.size() > start ? keys.size() - start : 0;
        const uint64_t to_take = std::min<uint64_t>(available, per_iteration);

        resp.result.reserve(to_take);

        auto i = iterator;
        while (i < iterator + per_iteration && i < keys.size()) {
            const auto &key = keys[i];
            resp.result.emplace_back(key, get_one(key, attributes));
            i++;
        }
        resp.iterator_version = this->iterator_version;
        resp.iterator = i < keys.size() ? i : 0;
        return glz::write_json(resp).value_or("error"); // TODO don't use json
    }


    void begin_transaction(uint64_t estimated_number_of_items = 0, bool remove_old_items = true) {
        if (transactionOpened) {
            throw std::runtime_error("Transaction already open");
        }
        if (estimated_number_of_items != 0) {
            cache.reserve(estimated_number_of_items);
            increaseIteratorVersion();
        }
        oldCacheSize = cache.size();
        transactionOpened = true;
        transactionShouldRemoveOldItems = remove_old_items;
    }

    void end_transaction() {
        if (!transactionOpened) {
            throw std::runtime_error("Transaction not opened");
        }
        for (auto it = cache.begin(); it != cache.end();) {
            if (it->second.isNew) {
                it.value().isNew = false;
                ++it;
            } else {
                if (transactionShouldRemoveOldItems) {
                    it = cache.erase(it);
                } else {
                    ++it;
                }
            }
        }
        increaseIteratorVersion();
        transactionOpened = false;
        transactionShouldRemoveOldItems = true;
    }

    size_t load_page(const str &json_text) {
        if (!transactionOpened) {
            throw std::runtime_error("Transaction not opened");
        }
        json::Response resp;
        if (auto ce = glz::read<glz::opts{.error_on_unknown_keys = false}>(resp, json_text))
            throw std::runtime_error(glz::format_error(ce, json_text));
        cache.reserve(resp.result.count);
        for (auto &item: resp.result.data) {
            auto &marked_item = cache[item.id];
            std::unordered_map<str, pyAttrValue> attrs;
            for (auto &attr: item.attributes)
                attrs[attr.id] = convert_valueJ(attr.value);

            setMarkedItem(marked_item, attrs);
        }
        increaseIteratorVersion();
        return resp.result.pagination.pages;
    }

    void increaseIteratorVersion() {
        iterator_version++;
        iterator_version++;
    }

    tsl::sparse_map<str, MarkedItem> cache;
    absl::flat_hash_map<str, uint8_t, absl::Hash<str>> attrMap;
    strVec attrIdx;
    const uint8_t numberOfAttributes;
    size_t oldCacheSize = 0;
    bool transactionOpened = false;
    bool transactionShouldRemoveOldItems = true;
    uint64_t iterator_version = 1; // for get_all()
};


NB_MODULE(_small_cache_impl, m) {
    nb::class_<SmallCache> cache(m, "SmallCache");
    cache
            .def(nb::init<std::vector<std::string> >(), nb::arg("attribute_names"))
            .def("begin_transaction", &SmallCache::begin_transaction,
                 nb::arg("estimated_number_of_items") = 0,
                 nb::arg("remove_old_items") = true)
            .def("end_transaction", &SmallCache::end_transaction)
            .def("add", &SmallCache::add_item, nb::arg("item_id"), nb::arg("attributes"))
            .def("get_one", &SmallCache::get_one, nb::arg("id"), nb::arg("attributes"))
            .def("get_many", &SmallCache::get_many, nb::arg("ids"), nb::arg("attributes"), nb::arg("per_iteration") = 10000,
                 nb::arg("iterator_version") = 0,
                 nb::arg("iterator") = 0)
            .def("load_page", &SmallCache::load_page, nb::arg("json_text"));
    // nb::class_<SmallCache::getAllResponse>(cache, "getAllResponse")
    //         .def(nb::init<>())
    //         .def_rw("iterator_version", &SmallCache::getAllResponse::iterator_version)
    //         .def_rw("iterator", &SmallCache::getAllResponse::iterator)
    //         .def_rw("result", &SmallCache::getAllResponse::result);
}
