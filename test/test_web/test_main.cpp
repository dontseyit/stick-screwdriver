// -----------------------------------------------------------------------------
//  Host tests for the web console's page registry.  pio test -e native
//
//  The first thing outside mathx/ that the host build compiles, because the
//  questions worth asking about a console are questions about a list: which
//  page answers a request, whether two claim one route, what belongs in a
//  footer. None of that needs a socket.
//
//  The traps are the reason for the tests. A route registered twice would be
//  resolved by link order, which is to say by luck. A page appearing in its own
//  footer offers a link back to where you already are. A form posting to the
//  path it came from shares that path with a page. And a feature's pages sit
//  under the feature's own path, so /notes must not answer for /notes/add.
// -----------------------------------------------------------------------------
#include <unity.h>

#include <cstdio>

#include "web/PageRegistry.h"

using namespace sd;

namespace {

/// A page body that is never run. Every test here is about the list, and Page
/// is only forward-declared in the header under test, which is the point.
void nothing(Page&) {}

PageInfo page(const char* path, Method m = Method::Get, const char* nav = nullptr) {
    PageInfo p;
    p.path   = path;
    p.method = m;
    p.title  = "T";
    p.nav    = nav;
    p.fn     = nothing;
    return p;
}

PageRegistry& fresh() {
    PageRegistry& r = PageRegistry::instance();
    r.clear();
    return r;
}

}  // namespace

// -----------------------------------------------------------------------------
//  Taking a page
// -----------------------------------------------------------------------------
void test_a_registered_page_can_be_found_again(void) {
    PageRegistry&  r = fresh();
    const PageInfo p = page("/rides");
    TEST_ASSERT_TRUE(r.add(p));
    TEST_ASSERT_EQUAL_UINT32(1u, r.count());
    TEST_ASSERT_EQUAL_PTR(&p, r.find("/rides", Method::Get));
}

void test_a_path_nobody_registered_is_not_found(void) {
    PageRegistry&  r = fresh();
    const PageInfo p = page("/rides");
    r.add(p);
    TEST_ASSERT_NULL(r.find("/places", Method::Get));
    TEST_ASSERT_NULL(r.find(nullptr, Method::Get));
}

void test_the_method_is_part_of_the_question(void) {
    // /add posts to the page / serves. They are two pages, and looking one up
    // must never hand back the other.
    PageRegistry&  r    = fresh();
    const PageInfo get  = page("/", Method::Get);
    const PageInfo post = page("/", Method::Post);
    TEST_ASSERT_TRUE(r.add(get));
    TEST_ASSERT_TRUE(r.add(post));
    TEST_ASSERT_EQUAL_PTR(&get, r.find("/", Method::Get));
    TEST_ASSERT_EQUAL_PTR(&post, r.find("/", Method::Post));
}

void test_one_route_cannot_be_claimed_twice(void) {
    // Refused rather than overwritten. Two pages on one route is a mistake
    // somebody has to see, and whichever won would depend on link order.
    PageRegistry&  r     = fresh();
    const PageInfo first = page("/rides");
    const PageInfo again = page("/rides");
    TEST_ASSERT_TRUE(r.add(first));
    TEST_ASSERT_FALSE(r.add(again));
    TEST_ASSERT_EQUAL_UINT32(1u, r.count());
    TEST_ASSERT_EQUAL_PTR(&first, r.find("/rides", Method::Get));
}

void test_a_path_under_another_page_is_its_own_page(void) {
    // The pages of one feature sit under its own path - /notes, /notes/add,
    // /notes/del - so a lookup must match the whole path and not a prefix of it.
    // Matched loosely, a POST to /notes/add would find the page that lists them.
    PageRegistry&  r    = fresh();
    const PageInfo list = page("/notes", Method::Get, "notes");
    const PageInfo add  = page("/notes/add", Method::Post);
    TEST_ASSERT_TRUE(r.add(list));
    TEST_ASSERT_TRUE(r.add(add));

    TEST_ASSERT_EQUAL_PTR(&list, r.find("/notes", Method::Get));
    TEST_ASSERT_EQUAL_PTR(&add, r.find("/notes/add", Method::Post));
    TEST_ASSERT_NULL(r.find("/notes/add", Method::Get));
    TEST_ASSERT_NULL(r.find("/notes/", Method::Get));
}

void test_a_page_that_could_never_answer_is_refused(void) {
    PageRegistry& r = fresh();

    PageInfo noPath = page("/x");
    noPath.path     = nullptr;
    TEST_ASSERT_FALSE(r.add(noPath));

    PageInfo noFn = page("/x");
    noFn.fn       = nullptr;
    TEST_ASSERT_FALSE(r.add(noFn));

    // A path the server would never match, every request starting with a slash.
    const PageInfo noSlash = page("rides");
    TEST_ASSERT_FALSE(r.add(noSlash));

    TEST_ASSERT_EQUAL_UINT32(0u, r.count());
}

void test_the_registry_stops_rather_than_overruns(void) {
    PageRegistry& r = fresh();
    PageInfo      held[PageRegistry::kMaxPages + 2];
    char          paths[PageRegistry::kMaxPages + 2][8];

    for (size_t i = 0; i < PageRegistry::kMaxPages + 2; ++i) {
        std::snprintf(paths[i], sizeof(paths[i]), "/p%u", static_cast<unsigned>(i));
        held[i] = page(paths[i]);
        const bool ok = r.add(held[i]);
        TEST_ASSERT_EQUAL(i < PageRegistry::kMaxPages, ok);
    }
    TEST_ASSERT_EQUAL_UINT32(PageRegistry::kMaxPages, r.count());
}

// -----------------------------------------------------------------------------
//  What belongs in a footer
// -----------------------------------------------------------------------------
void test_a_page_does_not_link_to_itself(void) {
    PageRegistry&  r = fresh();
    const PageInfo a = page("/", Method::Get, "notes");
    const PageInfo b = page("/rides", Method::Get, "rides");
    r.add(a);
    r.add(b);

    const PageInfo* nav[4];
    TEST_ASSERT_EQUAL_INT(1, r.navFor("/rides", nav, 4));
    TEST_ASSERT_EQUAL_PTR(&a, nav[0]);
}

void test_a_page_with_no_label_is_not_somewhere_to_go(void) {
    // A picture and a CSV are reached from a page, never from a list of places.
    // Only a page that named itself appears.
    PageRegistry&  r   = fresh();
    const PageInfo png = page("/ride.png");
    const PageInfo csv = page("/ride.csv");
    const PageInfo pg  = page("/rides", Method::Get, "rides");
    r.add(png);
    r.add(csv);
    r.add(pg);

    const PageInfo* nav[4];
    TEST_ASSERT_EQUAL_INT(1, r.navFor("/", nav, 4));
    TEST_ASSERT_EQUAL_PTR(&pg, nav[0]);
}

void test_a_form_does_not_put_the_page_it_returns_to_in_its_own_footer(void) {
    // POST / and GET / are different pages sharing one path. Compared by
    // pointer rather than path, the form would list the page it posts back to.
    PageRegistry&  r    = fresh();
    const PageInfo get  = page("/", Method::Get, "notes");
    const PageInfo post = page("/", Method::Post);
    r.add(get);
    r.add(post);

    const PageInfo* nav[4];
    TEST_ASSERT_EQUAL_INT(0, r.navFor("/", nav, 4));
}

void test_a_footer_never_writes_past_what_it_was_given(void) {
    PageRegistry& r = fresh();
    PageInfo      held[5];
    char          paths[5][8];
    for (int i = 0; i < 5; ++i) {
        std::snprintf(paths[i], sizeof(paths[i]), "/p%d", i);
        held[i] = page(paths[i], Method::Get, "somewhere");
        r.add(held[i]);
    }

    const PageInfo* nav[2];
    TEST_ASSERT_EQUAL_INT(2, r.navFor("/p0", nav, 2));
    TEST_ASSERT_EQUAL_INT(0, r.navFor("/p0", nav, 0));
}

void test_an_empty_console_has_nowhere_to_go(void) {
    PageRegistry&   r = fresh();
    const PageInfo* nav[4];
    TEST_ASSERT_EQUAL_INT(0, r.navFor("/", nav, 4));
    TEST_ASSERT_EQUAL_UINT32(0u, r.count());
}

// -----------------------------------------------------------------------------
int main(int, char**) {
    UNITY_BEGIN();

    RUN_TEST(test_a_registered_page_can_be_found_again);
    RUN_TEST(test_a_path_nobody_registered_is_not_found);
    RUN_TEST(test_the_method_is_part_of_the_question);
    RUN_TEST(test_one_route_cannot_be_claimed_twice);
    RUN_TEST(test_a_path_under_another_page_is_its_own_page);
    RUN_TEST(test_a_page_that_could_never_answer_is_refused);
    RUN_TEST(test_the_registry_stops_rather_than_overruns);

    RUN_TEST(test_a_page_does_not_link_to_itself);
    RUN_TEST(test_a_page_with_no_label_is_not_somewhere_to_go);
    RUN_TEST(test_a_form_does_not_put_the_page_it_returns_to_in_its_own_footer);
    RUN_TEST(test_a_footer_never_writes_past_what_it_was_given);
    RUN_TEST(test_an_empty_console_has_nowhere_to_go);

    return UNITY_END();
}
