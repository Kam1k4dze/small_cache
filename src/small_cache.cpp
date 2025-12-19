#include <nanobind/nanobind.h>
#include <nanobind/stl/variant.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/vector.h>
#include <nanobind/stl/optional.h>
#include <nanobind/stl/unordered_map.h>
#include <tsl/sparse_map.h>
#include <absl/hash/hash.h>
#include <absl/container/flat_hash_map.h>
#include <boost/flyweight.hpp>
#include <optional>
#include <algorithm>
#include <variant>
#include <vector>
#include <string>
#include <glaze/glaze.hpp>
#include "SmallCache.h"

namespace nb = nanobind;
using namespace nb::literals;

NB_MODULE(_small_cache_impl, m)
{
    nb::class_<SmallCache> cache(m, "SmallCache");
    cache
        .def(nb::init<std::vector<std::string>>(), nb::arg("attribute_names"))
        .def("begin_transaction", &SmallCache::begin_transaction,
             nb::arg("estimated_number_of_items") = 0,
             nb::arg("remove_old_items") = true)
        .def("end_transaction", &SmallCache::end_transaction)
        .def("add", &SmallCache::add_item, nb::arg("item_id"), nb::arg("attributes"))
        .def("get_one", &SmallCache::get_one, nb::arg("id"), nb::arg("attributes"))
        .def("get_many", &SmallCache::get_many, nb::arg("ids"), nb::arg("attributes"))
        .def("get_all_ids", &SmallCache::get_all_ids)
        .def("load_page", &SmallCache::load_page, nb::arg("json_text"));
}
