#include "SmallCache.h"
#include <print>
#include <ranges>
#include <algorithm>
#include <bit>
#include <unordered_set>

namespace
{
    template <class... Ts>
    struct overloaded : Ts...
    {
        using Ts::operator()...;
    };

    template <class... Ts>
    overloaded(Ts...) -> overloaded<Ts...>;

    std::string human_readable_size(size_t bytes)
    {
        constexpr const char* units[] = {"B", "KiB", "MiB", "GiB", "TiB"};
        auto v = static_cast<double>(bytes);
        int i = 0;
        while (v >= 1024.0 && i < 4)
        {
            v /= 1024.0;
            ++i;
        }
        return std::format("{:.2f} {}", v, units[i]);
    }
}

SmallCache::SmallCache(const strVec& attributes) : numberOfAttributes(attributes.size())
{
    if (attributes.empty())
    {
        throw std::runtime_error("No attributes provided");
    }
    if (attributes.size() > MarkedItem::maxAttributes)
    {
        throw std::runtime_error("Too many attributes provided");
    }
    attrMap.reserve(attributes.size());
    attrIdx.reserve(attributes.size());
    for (size_t idx = 0; idx < attributes.size(); ++idx)
    {
        const auto& attr = attributes[idx];
        attrIdx.emplace_back(attr);
        attrMap.emplace(attr, idx);
    }
}

std::vector<size_t> SmallCache::MarkedItem::getIdxs() const
{
    std::vector<size_t> idxs;
    idxs.reserve(value.size());

    for (size_t w = 0; w < attrs_flags.size(); ++w)
    {
        uint32_t bits = attrs_flags[w];
        while (bits)
        {
            unsigned b = std::countr_zero(bits);
            idxs.push_back(w * 32 + b);
            bits &= bits - 1; // clear that bit
        }
    }
    return idxs;
}

std::optional<std::reference_wrapper<SmallCache::AttributeValue>> SmallCache::MarkedItem::getValue(size_t idx) noexcept
{
    if (!hasIdx(idx))
        return std::nullopt;

    // count how many bits are set before 'idx'
    size_t w = idx / 32, b = idx % 32;
    size_t pos = 0;

    // sum full words
    for (size_t i = 0; i < w; ++i)
    {
        pos += std::popcount(attrs_flags[i]);
    }
    // sum lower bits in the same word
    if (b > 0)
    {
        uint32_t mask = (1u << b) - 1;
        pos += std::popcount(attrs_flags[w] & mask);
    }

    return value.size() > pos ? std::optional{std::ref(value[pos])} : std::nullopt; // just-in-case
}

std::optional<std::reference_wrapper<const SmallCache::AttributeValue>> SmallCache::MarkedItem::getValue(
    size_t idx) const noexcept
{
    if (auto ref = const_cast<MarkedItem*>(this)->getValue(idx))
        return std::cref(ref->get());
    return std::nullopt;
}

void SmallCache::setMarkedItem(MarkedItem& item, const std::unordered_map<str, pyAttrValue>& attrs)
{
    item.isNew = true;
    item.attrs_flags.fill(0);

    // build a slot for each possible attr index
    std::vector<std::optional<AttributeValue>> slots(attrMap.size());

    // 1) collect into slots[] by index
    for (auto& [name, pyVal] : attrs)
    {
        if (auto it = attrMap.find(name); it != attrMap.end())
        {
            slots[it->second] = convert_value(pyVal);
        }
    }

    // 2) reserve exactly as many as we’ll push
    auto count = std::ranges::count_if(slots, [](auto& o) { return o.has_value(); });
    item.value.clear();
    item.value.reserve(count);

    // 3) walk slots in ascending idx order,
    //    set flags and move values into item.value
    for (size_t idx = 0; idx < slots.size(); ++idx)
    {
        if (auto& opt = slots[idx]; opt)
        {
            item.value.push_back(std::move(*opt));
            auto w = idx / 32;
            auto b = idx % 32;
            item.attrs_flags[w] |= (1u << b);
        }
    }
}

SmallCache::AttributeValue SmallCache::convert_value(const json::AttributeValue& src)
{
    return std::visit(overloaded{
                          [](bool b) -> AttributeValue { return b; },
                          [](double d) -> AttributeValue { return d; },
                          [](const str& s) -> AttributeValue
                          {
                              // if (s.empty()) return std::monostate{};
                              return fwStr{s};
                          },

                          [](const std::vector<glz::raw_json>& json_vec) -> AttributeValue
                          {
                              auto out = std::make_unique<std::vector<fwStr>>();
                              out->reserve(json_vec.size());
                              for (auto& r : json_vec)
                                  if (!r.str.empty())
                                      out->emplace_back(r.str);
                              // if (out->empty()) {
                              //     return std::monostate{};
                              // }
                              out->shrink_to_fit();
                              return out;
                          },
                          [](const std::optional<glz::raw_json>& o) -> AttributeValue
                          {
                              // if (!o.has_value() || o->str.empty()) return std::monostate{};
                              return fwStr{o->str};
                          },

                      },
                      src);
}

SmallCache::pyAttrValue SmallCache::convert_valueJ(const json::AttributeValue& src)
{
    return std::visit(overloaded{
                          [](std::monostate b) -> pyAttrValue { return b; },
                          [](bool b) -> pyAttrValue { return b; },
                          [](double d) -> pyAttrValue { return d; },
                          [](const str& s) -> pyAttrValue { return s; },
                          [](const std::vector<glz::raw_json>& json_vec) -> pyAttrValue
                          {
                              return json_vec | std::views::transform([](const glz::raw_json& j) -> str
                                  {
                                      return j.str;
                                  }) |
                                  std::ranges::to<strVec>();
                          },
                          [](const std::optional<glz::raw_json>& o) -> pyAttrValue { return o ? o->str : ""; },

                      },
                      src);
}

SmallCache::pyAttrValue SmallCache::convert_value(const AttributeValue& src)
{
    return std::visit(overloaded{
                          [](std::monostate b) -> pyAttrValue { return b; },
                          [](bool b) -> pyAttrValue { return b; },
                          [](double d) -> pyAttrValue { return d; },
                          [](const fwStr& s) -> pyAttrValue { return s; },
                          [](const strVecUPtr& fw_vec) -> pyAttrValue
                          {
                              return *fw_vec | std::views::transform([](const fwStr& s) -> str { return s; }) |
                                  std::ranges::to<strVec>();
                          },
                      },
                      src);
}

SmallCache::AttributeValue SmallCache::convert_value(const pyAttrValue& src)
{
    return std::visit(overloaded{
                          [](std::monostate b) -> AttributeValue { return b; },
                          [](bool b) -> AttributeValue { return b; },
                          [](double d) -> AttributeValue { return d; },
                          [](const str& s) -> AttributeValue { return fwStr{s}; },
                          [](const strVec& vec) -> AttributeValue
                          {
                              auto out = std::make_unique<std::vector<fwStr>>();
                              out->reserve(vec.size());
                              for (auto& r : vec)
                              {
                                  out->emplace_back(r);
                              }
                              out->shrink_to_fit();
                              return out;
                          },
                      },
                      src);
}

std::string SmallCache::to_string(const pyAttrValue& src)
{
    return std::visit(overloaded{
                          [](std::monostate) -> str { return "null"; },
                          [](bool b) -> str { return b ? "true" : "false"; },
                          [](double d) -> str { return std::to_string(d); },
                          [](const str& s) -> str { return s; },

                          [](const strVec& vecptr) -> str
                          {
                              str out = "[";

                              for (auto& r : vecptr)
                              {
                                  out.append(r);
                                  out.append(",");
                              }
                              out.append("]");
                              return out;
                          },
                      },
                      src);
}

void SmallCache::add_item(const str& item_id, const std::unordered_map<str, pyAttrValue>& attributes)
{
    if (!transactionOpened)
    {
        throw std::runtime_error("Transaction not opened");
    }
    auto& marked_attrs = cache[item_id];
    setMarkedItem(marked_attrs, attributes);
}

std::vector<SmallCache::pyAttrValue> SmallCache::get_one(const str& id, const strVec& attributes)
{
    if (attributes.empty())
    {
        return {};
    }
    if (cache.contains(id))
    {
        const auto& item = cache.at(id);
        return attributes | std::views::transform([this, &item](const auto& attr_name) -> pyAttrValue
            {
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

std::vector<std::vector<SmallCache::pyAttrValue>> SmallCache::get_many(const strVec& ids, const strVec& attributes)
{
    std::vector<std::vector<pyAttrValue>> out;
    out.resize(ids.size());
    for (size_t i = 0; i < ids.size(); ++i)
    {
        out[i] = get_one(ids[i], attributes);
    }
    return out;
}

std::vector<std::string> SmallCache::get_all_ids()
{
    std::vector<str> keys = cache | std::views::keys | std::ranges::to<std::vector>();
    return keys;
}

void SmallCache::begin_transaction(uint64_t estimated_number_of_items, bool remove_old_items)
{
    if (transactionOpened)
    {
        throw std::runtime_error("Transaction already open");
    }
    if (estimated_number_of_items != 0)
    {
        cache.reserve(estimated_number_of_items);
    }
    oldCacheSize = cache.size();
    transactionOpened = true;
    transactionShouldRemoveOldItems = remove_old_items;
}

void SmallCache::end_transaction()
{
    if (!transactionOpened)
    {
        throw std::runtime_error("Transaction not opened");
    }
    for (auto it = cache.begin(); it != cache.end();)
    {
        if (it->second.isNew)
        {
            it.value().isNew = false;
            ++it;
        }
        else
        {
            if (transactionShouldRemoveOldItems)
            {
                it = cache.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }
    transactionOpened = false;
    transactionShouldRemoveOldItems = true;
}

size_t SmallCache::load_page(const str& json_text)
{
    if (!transactionOpened)
    {
        throw std::runtime_error("Transaction not opened");
    }
    json::Response resp;
    if (auto ce = glz::read<glz::opts{.error_on_unknown_keys = false}>(resp, json_text))
        throw std::runtime_error(glz::format_error(ce, json_text));
    cache.reserve(resp.result.count);
    for (auto& item : resp.result.data)
    {
        auto& marked_item = cache[item.id];
        std::unordered_map<str, pyAttrValue> attrs;
        for (auto& attr : item.attributes)
            attrs[attr.id] = convert_valueJ(attr.value);

        setMarkedItem(marked_item, attrs);
    }
    return resp.result.pagination.pages;
}

void SmallCache::print_variant_stats() const
{
    struct CountBytes
    {
        size_t count = 0;
        size_t heap_bytes = 0;
    };
    CountBytes s_null, s_double, s_bool, s_fw, s_vec;

    constexpr size_t slot_size = sizeof(AttributeValue);
    size_t total_values = 0;

    std::unordered_set<std::string> unique_strings;
    unique_strings.reserve(65536);

    // collect counts and heap-only bytes for vector payloads and interned strings
    for (const auto& kv : cache)
    {
        const auto& marked = kv.second;
        for (const auto& val : marked.value)
        {
            ++total_values;
            std::visit(overloaded{
                           [&](std::monostate)
                           {
                               ++s_null.count;
                           },
                           [&](double)
                           {
                               ++s_double.count;
                           },
                           [&](bool)
                           {
                               ++s_bool.count;
                           },
                           [&](const fwStr& fws)
                           {
                               ++s_fw.count;
                               unique_strings.emplace(static_cast<const std::string&>(fws));
                           },
                           [&](const strVecUPtr& vecptr)
                           {
                               ++s_vec.count;
                               if (vecptr)
                               {
                                   s_vec.heap_bytes += sizeof(std::vector<fwStr>);
                                   s_vec.heap_bytes += vecptr->capacity() * sizeof(fwStr);
                                   for (const auto& fws : *vecptr)
                                   {
                                       unique_strings.emplace(static_cast<const std::string&>(fws));
                                   }
                               }
                           }
                       }, val);
        }
    }

    // estimate heap used by unique interned strings (avoid attributing SSO as heap)
    constexpr std::size_t SSO_THRESHOLD = 15;
    size_t unique_strings_heap = 0;
    for (const auto& s : unique_strings)
    {
        if (s.capacity() > SSO_THRESHOLD)
        {
            unique_strings_heap += s.capacity() + 1; // buffer + NUL
        }
        constexpr size_t intern_node_overhead = 32;
        unique_strings_heap += intern_node_overhead;
    }

    const auto slot_bytes = [&](size_t count) { return count * slot_size; };

    size_t total_slot_bytes = total_values * slot_size;
    size_t total_heap_bytes = s_vec.heap_bytes + unique_strings_heap;
    size_t grand_total = total_slot_bytes + total_heap_bytes;

    std::println("{:<34}{:>12}{:>16}{:>16}{:>16}", "Variant", "count", "slot bytes", "heap bytes", "human(total)");
    std::println("{:-<94}", "");
    auto human_line = [&](size_t sbytes, size_t hbytes)
    {
        return human_readable_size(sbytes + hbytes);
    };

    std::println("{:<34}{:>12}{:>16}{:>16}{:>16}",
                 "null", s_null.count, slot_bytes(s_null.count), 0ULL, human_line(slot_bytes(s_null.count), 0));
    std::println("{:<34}{:>12}{:>16}{:>16}{:>16}",
                 "double", s_double.count, slot_bytes(s_double.count), 0ULL, human_line(slot_bytes(s_double.count), 0));
    std::println("{:<34}{:>12}{:>16}{:>16}{:>16}",
                 "bool", s_bool.count, slot_bytes(s_bool.count), 0ULL, human_line(slot_bytes(s_bool.count), 0));
    std::println("{:<34}{:>12}{:>16}{:>16}{:>16}",
                 "fwStr (handles)", s_fw.count, slot_bytes(s_fw.count), 0ULL, human_line(slot_bytes(s_fw.count), 0));
    std::println("{:<34}{:>12}{:>16}{:>16}{:>16}",
                 "vector<fwStr> (heap)", s_vec.count, slot_bytes(s_vec.count), s_vec.heap_bytes,
                 human_line(slot_bytes(s_vec.count), s_vec.heap_bytes));
    std::println("{:-<94}", "");
    std::println("{:<34}{:>12}{:>16}{:>16}{:>16}",
                 "unique interned strings", unique_strings.size(), 0ULL, unique_strings_heap,
                 human_line(0, unique_strings_heap));
    std::println("{:=<94}", "");
    std::println("{:<34}{:>12}{:>16}{:>16}{:>16}",
                 "variant storage (slots)", total_values, total_slot_bytes, 0ULL,
                 human_readable_size(total_slot_bytes));
    std::println("{:<34}{:>12}{:>16}{:>16}{:>16}",
                 "heap only (vectors + intern pool)", "", 0ULL, total_heap_bytes,
                 human_readable_size(total_heap_bytes));
    std::println("{:<34}{:>12}{:>16}{:>16}{:>16}",
                 "TOTAL (approx)", "", total_slot_bytes, total_heap_bytes, human_readable_size(grand_total));
}
