#include <gtest/gtest.h>
#include "SmallCache.h"
#include <vector>
#include <string>
#include <variant>
#include <algorithm>
#include <format>

using namespace std::string_literals;

class SmallCacheTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // Code here will be called immediately after the constructor (right
        // before each test).
    }

    void TearDown() override
    {
        // Code here will be called immediately after each test (right
        // before the destructor).
    }
};

TEST_F(SmallCacheTest, Constructor)
{
    std::vector<std::string> attrs = {"attr1", "attr2"};
    EXPECT_NO_THROW(SmallCache cache(attrs));

    // Test empty attributes
    EXPECT_THROW(SmallCache({}), std::runtime_error);

    // Test too many attributes
    std::vector<std::string> many_attrs;
    for (int i = 0; i < 97; ++i)
    {
        many_attrs.push_back("attr" + std::to_string(i));
    }
    EXPECT_THROW(SmallCache cache(many_attrs), std::runtime_error);

    // Test max attributes (96) - should succeed
    std::vector<std::string> max_attrs;
    for (int i = 0; i < 96; ++i)
    {
        max_attrs.push_back("attr" + std::to_string(i));
    }
    EXPECT_NO_THROW(SmallCache cache(max_attrs));

    // Test duplicate attributes
    // The implementation allows duplicates but maps them to the first index.
    // We should verify this behavior or at least that it doesn't crash.
    std::vector<std::string> dup_attrs = {"A", "A"};
    SmallCache dup_cache(dup_attrs);
    dup_cache.begin_transaction();
    dup_cache.add_item("1", {{"A", 1.0}});
    dup_cache.end_transaction();
    auto res = dup_cache.get_one("1", dup_attrs);
    ASSERT_EQ(res.size(), 2);
    EXPECT_EQ(std::get<double>(res[0]), 1.0);
    EXPECT_EQ(std::get<double>(res[1]), 1.0);
}

TEST_F(SmallCacheTest, TransactionLifecycle)
{
    std::vector<std::string> attrs = {"attr1"};
    SmallCache cache(attrs);

    // Test add_item without transaction
    EXPECT_THROW(cache.add_item("id1", {}), std::runtime_error);

    // Test load_page without transaction
    EXPECT_THROW(cache.load_page("{}"), std::runtime_error);

    // Test end_transaction without transaction
    EXPECT_THROW(cache.end_transaction(), std::runtime_error);

    // Start transaction
    cache.begin_transaction();

    // Test double begin_transaction
    EXPECT_THROW(cache.begin_transaction(), std::runtime_error);

    // End transaction
    cache.end_transaction();
}

TEST_F(SmallCacheTest, AddGetItem)
{
    std::vector<std::string> attrs = {"bool_attr", "double_attr", "str_attr", "vec_attr", "null_attr"};
    SmallCache cache(attrs);

    cache.begin_transaction();

    std::unordered_map<std::string, SmallCache::pyAttrValue> item_attrs;
    item_attrs["bool_attr"] = true;
    item_attrs["double_attr"] = 123.45;
    item_attrs["str_attr"] = "hello"s;
    item_attrs["vec_attr"] = std::vector<std::string>{"a", "b"};
    // null_attr is missing/monostate

    cache.add_item("item1", item_attrs);

    // Test empty string and empty vector
    std::unordered_map<std::string, SmallCache::pyAttrValue> item_empty;
    item_empty["str_attr"] = ""s;
    item_empty["vec_attr"] = std::vector<std::string>{};
    cache.add_item("item_empty", item_empty);

    cache.end_transaction();

    // Test get_one
    auto res = cache.get_one("item1", attrs);
    ASSERT_EQ(res.size(), 5);

    ASSERT_TRUE(std::holds_alternative<bool>(res[0]));
    EXPECT_EQ(std::get<bool>(res[0]), true);

    ASSERT_TRUE(std::holds_alternative<double>(res[1]));
    EXPECT_EQ(std::get<double>(res[1]), 123.45);

    ASSERT_TRUE(std::holds_alternative<std::string>(res[2]));
    EXPECT_EQ(std::get<std::string>(res[2]), "hello");

    ASSERT_TRUE(std::holds_alternative<std::vector<std::string>>(res[3]));
    auto vec = std::get<std::vector<std::string>>(res[3]);
    ASSERT_EQ(vec.size(), 2);
    EXPECT_EQ(vec[0], "a");
    EXPECT_EQ(vec[1], "b");

    ASSERT_TRUE(std::holds_alternative<std::monostate>(res[4]));

    // Test empty values
    auto res_empty = cache.get_one("item_empty", attrs);
    ASSERT_TRUE(std::holds_alternative<std::string>(res_empty[2]));
    EXPECT_EQ(std::get<std::string>(res_empty[2]), "");

    ASSERT_TRUE(std::holds_alternative<std::vector<std::string>>(res_empty[3]));
    auto vec_empty = std::get<std::vector<std::string>>(res_empty[3]);
    EXPECT_TRUE(vec_empty.empty());

    // Test get_one with subset of attributes
    auto res_subset = cache.get_one("item1", {"str_attr"});
    ASSERT_EQ(res_subset.size(), 1);
    EXPECT_EQ(std::get<std::string>(res_subset[0]), "hello");

    // Test get_one with unknown attribute
    auto res_unknown = cache.get_one("item1", {"unknown_attr"});
    ASSERT_EQ(res_unknown.size(), 1);
    EXPECT_TRUE(std::holds_alternative<std::monostate>(res_unknown[0]));

    // Test get_one non-existent item
    auto res_missing = cache.get_one("missing_item", attrs);
    EXPECT_TRUE(res_missing.empty());
}

TEST_F(SmallCacheTest, GetMany)
{
    std::vector<std::string> attrs = {"val"};
    SmallCache cache(attrs);

    cache.begin_transaction();
    cache.add_item("1", {{"val", 1.0}});
    cache.add_item("2", {{"val", 2.0}});
    cache.end_transaction();

    auto res = cache.get_many({"1", "2", "3"}, attrs);
    ASSERT_EQ(res.size(), 3);

    ASSERT_EQ(res[0].size(), 1);
    EXPECT_EQ(std::get<double>(res[0][0]), 1.0);

    ASSERT_EQ(res[1].size(), 1);
    EXPECT_EQ(std::get<double>(res[1][0]), 2.0);

    EXPECT_TRUE(res[2].empty()); // Item 3 does not exist
}

TEST_F(SmallCacheTest, TransactionCleanup)
{
    std::vector<std::string> attrs = {"val"};
    SmallCache cache(attrs);

    // Add item 1
    cache.begin_transaction();
    cache.add_item("1", {{"val", 1.0}});
    cache.end_transaction();

    ASSERT_EQ(cache.get_all_ids().size(), 1);

    // Update item 1, add item 2. remove_old_items = true (default)
    cache.begin_transaction();
    cache.add_item("2", {{"val", 2.0}});
    // We don't add "1", so it should be removed
    cache.end_transaction();

    auto ids = cache.get_all_ids();
    ASSERT_EQ(ids.size(), 1);
    EXPECT_EQ(ids[0], "2");

    // Now test remove_old_items = false
    cache.begin_transaction(0, false);
    cache.add_item("3", {{"val", 3.0}});
    // We don't add "2", but it should be kept
    cache.end_transaction();

    ids = cache.get_all_ids();
    ASSERT_EQ(ids.size(), 2);
    // Check content
    std::sort(ids.begin(), ids.end());
    EXPECT_EQ(ids[0], "2");
    EXPECT_EQ(ids[1], "3");
}

TEST_F(SmallCacheTest, LoadPage)
{
    std::vector<std::string> attrs = {"code", "label"};
    SmallCache cache(attrs);

    std::string json = R"({
        "result": {
            "count": 1,
            "pagination": {"page": 1, "pages": 5},
            "data": [
                {
                    "id": "item1",
                    "attributes": [
                        {"id": "code", "value": "C123"},
                        {"id": "label", "value": "Label 123"},
                        {"id": "unknown", "value": 123}
                    ]
                }
            ]
        }
    })";

    cache.begin_transaction();
    size_t pages = cache.load_page(json);
    cache.end_transaction();

    EXPECT_EQ(pages, 5);
    auto res = cache.get_one("item1", attrs);
    ASSERT_EQ(res.size(), 2);
    EXPECT_EQ(std::get<std::string>(res[0]), "C123");
    EXPECT_EQ(std::get<std::string>(res[1]), "Label 123");
}

TEST_F(SmallCacheTest, LoadPageInvalidJson)
{
    SmallCache cache({"a"});
    cache.begin_transaction();
    EXPECT_THROW(cache.load_page("{ invalid json "), std::runtime_error);
    cache.end_transaction();
}

TEST_F(SmallCacheTest, ToString)
{
    EXPECT_EQ(SmallCache::to_string(std::monostate{}), "null");
    EXPECT_EQ(SmallCache::to_string(true), "true");
    EXPECT_EQ(SmallCache::to_string(false), "false");
    // EXPECT_EQ(SmallCache::to_string(1.23), "1.23");
    EXPECT_EQ(SmallCache::to_string(std::string("test")), "test");

    std::vector<std::string> vec = {"a", "b"};
    EXPECT_EQ(SmallCache::to_string(vec), "[a,b,]");
}

TEST_F(SmallCacheTest, PrintStats)
{
    SmallCache cache({"a"});
    cache.begin_transaction();
    cache.add_item("1", {{"a", 1.0}});
    cache.end_transaction();
    // Just ensure it doesn't crash
    cache.print_variant_stats();
}

TEST_F(SmallCacheTest, MarkedItemLogic)
{
    // Test sparse attributes and bitmask logic
    std::vector<std::string> attrs;
    for (int i = 0; i < 40; ++i) attrs.push_back("a" + std::to_string(i));

    SmallCache cache(attrs);
    cache.begin_transaction();

    std::unordered_map<std::string, SmallCache::pyAttrValue> item_attrs;
    item_attrs["a0"] = 1.0;
    item_attrs["a31"] = 31.0; // Boundary of first word
    item_attrs["a32"] = 32.0; // Start of second word
    item_attrs["a35"] = 2.0; // In second word of bitmask

    cache.add_item("i1", item_attrs);
    cache.end_transaction();

    auto res = cache.get_one("i1", attrs);
    EXPECT_EQ(std::get<double>(res[0]), 1.0);
    EXPECT_EQ(std::get<double>(res[31]), 31.0);
    EXPECT_EQ(std::get<double>(res[32]), 32.0);
    EXPECT_EQ(std::get<double>(res[35]), 2.0);
    EXPECT_TRUE(std::holds_alternative<std::monostate>(res[1]));
}

TEST_F(SmallCacheTest, LoadPageValidJsonExtended)
{
    std::vector<std::string> attrs = {"attr1", "attr2"};
    SmallCache cache(attrs);
    cache.begin_transaction();

    std::string json = R"({
        "result": {
            "count": 1,
            "pagination": {"page": 1, "pages": 1},
            "data": [
                {
                    "id": "item1",
                    "attributes": [
                        {"id": "attr1", "value": "val1"},
                        {"id": "attr2", "value": 123.0}
                    ]
                }
            ]
        }
    })";

    EXPECT_NO_THROW(cache.load_page(json));
    cache.end_transaction();

    auto res = cache.get_one("item1", attrs);
    ASSERT_EQ(res.size(), 2);
    EXPECT_EQ(std::get<std::string>(res[0]), "val1");
    EXPECT_EQ(std::get<double>(res[1]), 123.0);
}

TEST_F(SmallCacheTest, LoadPagePartialAttributes)
{
    std::vector<std::string> attrs = {"attr1", "attr2"};
    SmallCache cache(attrs);
    cache.begin_transaction();

    std::string json = R"({
        "result": {
            "count": 1,
            "pagination": {"page": 1, "pages": 1},
            "data": [
                {
                    "id": "item1",
                    "attributes": [
                        {"id": "attr1", "value": "val1"}
                    ]
                }
            ]
        }
    })";

    cache.load_page(json);
    cache.end_transaction();

    auto res = cache.get_one("item1", attrs);
    ASSERT_EQ(res.size(), 2);
    EXPECT_EQ(std::get<std::string>(res[0]), "val1");
    EXPECT_TRUE(std::holds_alternative<std::monostate>(res[1]));
}

TEST_F(SmallCacheTest, LoadPageExtraAttributes)
{
    std::vector<std::string> attrs = {"attr1"};
    SmallCache cache(attrs);
    cache.begin_transaction();

    std::string json = R"({
        "result": {
            "count": 1,
            "pagination": {"page": 1, "pages": 1},
            "data": [
                {
                    "id": "item1",
                    "attributes": [
                        {"id": "attr1", "value": "val1"},
                        {"id": "extra", "value": "ignored"}
                    ]
                }
            ]
        }
    })";

    cache.load_page(json);
    cache.end_transaction();

    auto res = cache.get_one("item1", attrs);
    ASSERT_EQ(res.size(), 1);
    EXPECT_EQ(std::get<std::string>(res[0]), "val1");
}

TEST_F(SmallCacheTest, SparseAttributesBitmaskExtended)
{
    std::vector<std::string> attrs;
    for (int i = 0; i < 96; ++i) attrs.push_back("a" + std::to_string(i));
    SmallCache cache(attrs);

    cache.begin_transaction();
    // Set first, middle, and last attribute
    std::unordered_map<std::string, SmallCache::pyAttrValue> item_attrs;
    item_attrs["a0"] = 1.0;
    item_attrs["a48"] = 2.0;
    item_attrs["a95"] = 3.0;

    cache.add_item("item1", item_attrs);
    cache.end_transaction();

    auto res = cache.get_one("item1", attrs);
    ASSERT_EQ(res.size(), 96);

    EXPECT_EQ(std::get<double>(res[0]), 1.0);
    EXPECT_EQ(std::get<double>(res[48]), 2.0);
    EXPECT_EQ(std::get<double>(res[95]), 3.0);

    // Check some in between are empty
    EXPECT_TRUE(std::holds_alternative<std::monostate>(res[1]));
    EXPECT_TRUE(std::holds_alternative<std::monostate>(res[47]));
    EXPECT_TRUE(std::holds_alternative<std::monostate>(res[49]));
    EXPECT_TRUE(std::holds_alternative<std::monostate>(res[94]));
}

TEST_F(SmallCacheTest, TransactionKeepOldItemsExtended)
{
    std::vector<std::string> attrs = {"val"};
    SmallCache cache(attrs);

    // Transaction 1: Add item A
    cache.begin_transaction();
    cache.add_item("A", {{"val", 1.0}});
    cache.end_transaction();

    ASSERT_EQ(cache.get_all_ids().size(), 1);

    // Transaction 2: Add item B, keep old items
    cache.begin_transaction(0, false); // remove_old_items = false
    cache.add_item("B", {{"val", 2.0}});
    cache.end_transaction();

    auto ids = cache.get_all_ids();
    ASSERT_EQ(ids.size(), 2);

    // Transaction 3: Update item A, keep old items
    cache.begin_transaction(0, false);
    cache.add_item("A", {{"val", 3.0}});
    cache.end_transaction();

    ids = cache.get_all_ids();
    ASSERT_EQ(ids.size(), 2);

    auto resA = cache.get_one("A", attrs);
    EXPECT_EQ(std::get<double>(resA[0]), 3.0);

    auto resB = cache.get_one("B", attrs);
    EXPECT_EQ(std::get<double>(resB[0]), 2.0);
}
