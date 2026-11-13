#include <unity.h>
#include <ut_touch.h>
#include <initializer_list>
using namespace ut_meter;
void setUp() {}
void tearDown() {}
void test_touch_actions_wait_for_release_and_keep_last_coordinates() {
    TouchGesture g;
    TEST_ASSERT_FALSE(g.update(40,200,true,true,0).tap);
    TEST_ASSERT_FALSE(g.update(42,198,true,true,30).tap);
    auto a=g.update(0,0,false,true,100);
    TEST_ASSERT_TRUE(a.tap);TEST_ASSERT_EQUAL(42,a.x);TEST_ASSERT_EQUAL(198,a.y);
}
void test_drag_over_button_scrolls_without_clicking() {
    TouchGesture g;
    g.update(40,200,true,true,0);
    auto a=g.update(40,160,true,true,20);
    TEST_ASSERT_EQUAL(40,a.deltaY);TEST_ASSERT_FALSE(a.tap);
    a=g.update(40,100,true,true,40);
    TEST_ASSERT_EQUAL(60,a.deltaY);
    TEST_ASSERT_FALSE(g.update(0,0,false,true,60).tap);
}
void test_horizontal_or_non_scrollable_drag_never_taps() {
    TouchGesture g;
    g.update(30,100,true,true,0);
    TEST_ASSERT_EQUAL(0,g.update(70,100,true,true,30).deltaY);
    TEST_ASSERT_FALSE(g.update(0,0,false,true,60).tap);
    g.update(30,100,true,false,1000);
    TEST_ASSERT_EQUAL(0,g.update(30,50,true,true,1050).deltaY);
    TEST_ASSERT_FALSE(g.update(0,0,false,true,1100).tap);
}
void test_duplicate_taps_are_debounced_across_clock_wrap() {
    TouchGesture g;
    g.update(1,1,true,false,0xffffffe0u);
    TEST_ASSERT_TRUE(g.update(0,0,false,false,0xfffffff0u).tap);
    g.update(1,1,true,false,10);
    TEST_ASSERT_FALSE(g.update(0,0,false,false,20).tap);
    g.update(1,1,true,false,300);
    TEST_ASSERT_TRUE(g.update(0,0,false,false,350).tap);
}
void test_up_swipe_is_release_only_directional_and_not_scroll(){
 TouchGesture g;g.update(100,230,true,false,0);auto a=g.update(108,160,true,false,20);TEST_ASSERT_FALSE(a.swipeUp);a=g.update(0,0,false,false,40);TEST_ASSERT_TRUE(a.swipeUp);TEST_ASSERT_FALSE(a.tap);TEST_ASSERT_FALSE(g.update(0,0,false,false,50).swipeUp);
 g.update(100,100,true,false,100);g.update(100,180,true,false,120);TEST_ASSERT_FALSE(g.update(0,0,false,false,140).swipeUp);
 g.update(100,230,true,false,200);g.update(100,200,true,false,220);TEST_ASSERT_FALSE(g.update(0,0,false,false,240).swipeUp);
 g.update(100,230,true,false,300);g.update(180,160,true,false,320);TEST_ASSERT_FALSE(g.update(0,0,false,false,340).swipeUp);
 g.update(100,230,true,true,400);g.update(100,160,true,true,420);TEST_ASSERT_FALSE(g.update(0,0,false,true,440).swipeUp);
}
void test_inward_edges_use_logical_bounds_and_fire_once_on_release(){
 for(const auto size : {320,480}){
  const int width=size,height=800-size;
  for(unsigned edge=0;edge<3;++edge){
   TouchGesture g;const int x=edge==1?8:edge==2?width-9:width/2,y=edge==0?8:height/2;
   const int endX=x+(edge==1?72:edge==2?-72:0),endY=y+(edge==0?72:0);
   g.update(x,y,true,false,0,width,height);
   auto a=g.update(endX,endY,true,false,20,width,height);TEST_ASSERT_EQUAL(int(TouchEdge::None),int(a.edge));
   a=g.update(0,0,false,false,40,width,height);
   TEST_ASSERT_EQUAL(int(edge==0?TouchEdge::Top:edge==1?TouchEdge::Left:TouchEdge::Right),int(a.edge));TEST_ASSERT_FALSE(a.tap);
   TEST_ASSERT_EQUAL(int(TouchEdge::None),int(g.update(0,0,false,false,50,width,height).edge));
  }
 }
}
void test_edges_reject_middle_short_outward_diagonal_and_scrolling_drags(){
 // start x,y; end x,y; scroll. A scrollable form always retains its drag.
 const int drags[][5]={{80,100,160,100,0},{100,80,100,160,0},{8,100,45,100,0},{16,100,0,100,0},{8,100,88,150,0},{8,100,88,100,1},{100,8,100,88,1},{311,100,240,100,1}};
 for(const auto &d:drags){TouchGesture g;g.update(d[0],d[1],true,d[4],0,320,480);g.update(d[2],d[3],true,d[4],20,320,480);const auto a=g.update(0,0,false,d[4],40,320,480);TEST_ASSERT_EQUAL(int(TouchEdge::None),int(a.edge));TEST_ASSERT_FALSE(a.tap);}
}
int main() {
    UNITY_BEGIN();
    RUN_TEST(test_touch_actions_wait_for_release_and_keep_last_coordinates);
    RUN_TEST(test_drag_over_button_scrolls_without_clicking);
    RUN_TEST(test_horizontal_or_non_scrollable_drag_never_taps);
    RUN_TEST(test_duplicate_taps_are_debounced_across_clock_wrap);
    RUN_TEST(test_up_swipe_is_release_only_directional_and_not_scroll);
    RUN_TEST(test_inward_edges_use_logical_bounds_and_fire_once_on_release);
    RUN_TEST(test_edges_reject_middle_short_outward_diagonal_and_scrolling_drags);
    return UNITY_END();
}
