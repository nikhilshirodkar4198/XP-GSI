#include <doctest/doctest.h>

#include "engine/FollowUser.h"

using FollowUser::Action;
using FollowUser::Input;

// Descending into KLAX with KSFO's statics still active.
static Input approach() {
    Input in;
    in.lowEnough = true;
    in.onGround = false;
    in.hasPlan = true;
    in.activeKm = 540.0;      // KSFO
    in.haveNearest = true;
    in.nearestIsActive = false;
    in.nearestKm = 40.0;      // KLAX
    return in;
}

TEST_SUITE("FollowUser") {

TEST_CASE("below 10,000 ft a nearer airline airport takes over") {
    CHECK(FollowUser::decide(approach()) == Action::Switch);
}

TEST_CASE("cruise: no switching; a plan left > 50 nm behind is dropped") {
    Input in = approach();
    in.lowEnough = false;
    CHECK(FollowUser::decide(in) == Action::Cleanup);
    in.activeKm = 60.0;                       // still near the departure
    CHECK(FollowUser::decide(in) == Action::None);
    in.activeKm = 540.0; in.hasPlan = false;  // already dropped
    CHECK(FollowUser::decide(in) == Action::None);
}

TEST_CASE("no switch on the ground, while the nearest is active, or right after a switch") {
    Input in = approach();
    in.onGround = true;
    CHECK(FollowUser::decide(in) == Action::None);
    in = approach(); in.nearestIsActive = true;
    CHECK(FollowUser::decide(in) == Action::None);
    in = approach(); in.sinceSwitchSec = 30.0;
    CHECK(FollowUser::decide(in) == Action::None);
    in = approach(); in.haveNearest = false;
    CHECK(FollowUser::decide(in) == Action::None);
}

TEST_CASE("hysteresis: the new airport must be 5 km nearer") {
    Input in = approach();
    in.activeKm = 20.0; in.nearestKm = 16.0;   // only 4 km nearer
    CHECK(FollowUser::decide(in) == Action::None);
    in.nearestKm = 14.0;
    CHECK(FollowUser::decide(in) == Action::Switch);
}

} // TEST_SUITE
