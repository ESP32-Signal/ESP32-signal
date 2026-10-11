#include "axle_counter.h"
#include "axle_sensor.h"
#include "config.h"

#include <stdio.h>

namespace {

int failures = 0;

void expect(bool cond, const char* name) {
  if (cond) {
    return;
  }
  fprintf(stderr, "FAIL %s\n", name);
  failures++;
}

void closeContact(bahn::AxleSensor& sensor, bool contactA, bool contactB, uint32_t start, bahn::AxleDirection& seen) {
  sensor.poll(contactA, contactB, start);
  const bahn::AxleDirection direction = sensor.poll(contactA, contactB, start + bahn::kAxleDebounceMs);
  if (direction != bahn::AxleDirection::None) {
    seen = direction;
  }
}

void testSensor() {
  bahn::AxleSensor sensor;
  expect(sensor.poll(false, false, 0) == bahn::AxleDirection::None, "sensor primes open");

  bahn::AxleDirection seen = bahn::AxleDirection::None;
  closeContact(sensor, true, false, 10, seen);
  expect(seen == bahn::AxleDirection::None, "first contact does not count");
  seen = bahn::AxleDirection::None;
  closeContact(sensor, true, true, 30, seen);
  expect(seen == bahn::AxleDirection::TowardB, "A then B counts toward B");

  seen = bahn::AxleDirection::None;
  closeContact(sensor, false, false, 50, seen);
  expect(seen == bahn::AxleDirection::None, "release does not count");

  seen = bahn::AxleDirection::None;
  closeContact(sensor, false, true, 80, seen);
  closeContact(sensor, true, true, 100, seen);
  expect(seen == bahn::AxleDirection::TowardA, "B then A counts toward A");
  closeContact(sensor, false, false, 120, seen);

  bahn::AxleSensor bounce;
  bounce.poll(false, false, 0);
  bounce.poll(true, false, 10);
  bounce.poll(false, false, 12);
  bounce.poll(true, false, 14);
  expect(bounce.poll(true, false, 14 + bahn::kAxleDebounceMs) == bahn::AxleDirection::None, "bounce is not an axle");

  bahn::AxleSensor retreat;
  retreat.poll(false, false, 0);
  closeContact(retreat, true, false, 10, seen);
  seen = bahn::AxleDirection::None;
  closeContact(retreat, false, false, 30, seen);
  expect(seen == bahn::AxleDirection::None, "contact opening is not an axle");

  bahn::AxleSensor both;
  both.poll(false, false, 0);
  seen = bahn::AxleDirection::None;
  closeContact(both, true, true, 10, seen);
  expect(seen == bahn::AxleDirection::None, "both contacts together are ignored");

  bahn::AxleSensor stuck;
  stuck.poll(false, false, 0);
  closeContact(stuck, true, false, 10, seen);
  const uint32_t expired = 10 + bahn::kAxleDebounceMs + bahn::kAxlePairWindowMs + 1;
  expect(stuck.poll(true, false, expired) == bahn::AxleDirection::None, "window expiry is quiet");
  seen = bahn::AxleDirection::None;
  closeContact(stuck, true, true, expired + 10, seen);
  expect(seen == bahn::AxleDirection::None, "late second contact is not an axle");

  bahn::AxleSensor twice;
  twice.poll(false, false, 0);
  seen = bahn::AxleDirection::None;
  closeContact(twice, true, false, 10, seen);
  closeContact(twice, true, true, 30, seen);
  expect(seen == bahn::AxleDirection::TowardB, "first of two axles");
  closeContact(twice, false, false, 50, seen);
  seen = bahn::AxleDirection::None;
  closeContact(twice, true, false, 80, seen);
  closeContact(twice, true, true, 100, seen);
  expect(seen == bahn::AxleDirection::TowardB, "second axle counts too");
}

void pass(bahn::AxleCounter& counter, bahn::AxleDirection direction, int axles, uint32_t nowMs) {
  for (int i = 0; i < axles; ++i) {
    counter.onPassage(direction, nowMs);
  }
}

void testCounter() {
  bahn::AxleCounter counter;
  pass(counter, bahn::AxleDirection::TowardB, 4, 0);
  uint16_t count = 0;
  expect(counter.takeVorblock(bahn::Side::B, count) && count == 4, "vorblock on entry");
  expect(counter.takeAchszahl(bahn::Side::B, count) && count == 4, "achszahl follows the count");
  expect(!counter.takeVorblock(bahn::Side::B, count), "one vorblock per entry");
  expect(counter.occupied(bahn::Side::B) && !counter.occupied(bahn::Side::A), "occupied toward B");
  expect(counter.countToward(bahn::Side::B) == 4, "count toward B");

  counter.onPassage(bahn::AxleDirection::TowardA, 20);
  expect(counter.takeAchszahl(bahn::Side::B, count) && count == 3, "rollback decrements");
  expect(!counter.takeVorblock(bahn::Side::B, count), "rollback is not a new entry");

  counter.onPassage(bahn::AxleDirection::TowardA, 30);
  counter.onPassage(bahn::AxleDirection::TowardA, 30);
  counter.onPassage(bahn::AxleDirection::TowardA, 30);
  expect(counter.net() == 0, "train rolled back out");
  expect(counter.takeAchszahl(bahn::Side::B, count) && count == 0, "clear count is announced");
  expect(!counter.occupied(bahn::Side::B), "clear after rollback");

  pass(counter, bahn::AxleDirection::TowardB, 1, 40);
  expect(counter.takeVorblock(bahn::Side::B, count) && count == 1, "next entry sends vorblock again");
  counter.takeAchszahl(bahn::Side::B, count);

  bahn::AxleCounter matched;
  matched.onRemote(bahn::Side::A, bahn::MsgType::Achszahl, 4, 0);
  pass(matched, bahn::AxleDirection::TowardB, 4, 0);
  matched.update(bahn::kAxleSettleMs - 1);
  expect(!matched.takeRueckblock(bahn::Side::A, count), "no rueckblock before settle");
  matched.onRemote(bahn::Side::A, bahn::MsgType::Achszahl, 4, 1000);
  matched.update(bahn::kAxleSettleMs);
  expect(matched.takeRueckblock(bahn::Side::A, count) && count == 4, "duplicate achszahl keeps the settle time");

  bahn::AxleCounter growing;
  growing.onRemote(bahn::Side::A, bahn::MsgType::Vorblock, 4, 0);
  expect(growing.approachFrom(bahn::Side::A), "vorblock is remembered");
  pass(growing, bahn::AxleDirection::TowardB, 4, 0);
  growing.onRemote(bahn::Side::A, bahn::MsgType::Achszahl, 5, 1400);
  growing.onPassage(bahn::AxleDirection::TowardB, 1400);
  growing.update(1400 + bahn::kAxleSettleMs - 1);
  expect(!growing.takeRueckblock(bahn::Side::A, count), "fresh achszahl restarts settle");
  growing.update(1400 + bahn::kAxleSettleMs);
  expect(growing.takeRueckblock(bahn::Side::A, count) && count == 5, "rueckblock after the new count settles");
  expect(!growing.approachFrom(bahn::Side::A), "approach ends when the block is released");

  bahn::AxleCounter released;
  pass(released, bahn::AxleDirection::TowardB, 4, 0);
  released.onRemote(bahn::Side::B, bahn::MsgType::Rueckblock, 4, bahn::kAxleSettleMs);
  expect(released.net() == 0 && !released.occupied(bahn::Side::B), "matching rueckblock clears the counter");
  pass(released, bahn::AxleDirection::TowardB, 2, bahn::kAxleSettleMs + 10);
  expect(released.takeVorblock(bahn::Side::B, count) && count == 2, "counter starts again after release");

  bahn::AxleCounter wrong;
  pass(wrong, bahn::AxleDirection::TowardB, 4, 0);
  wrong.onRemote(bahn::Side::B, bahn::MsgType::Rueckblock, 3, bahn::kAxleSettleMs);
  expect(wrong.net() == 4, "different rueckblock count is ignored");

  bahn::AxleCounter early;
  pass(early, bahn::AxleDirection::TowardB, 2, 0);
  early.onRemote(bahn::Side::B, bahn::MsgType::Rueckblock, 2, 100);
  early.onPassage(bahn::AxleDirection::TowardB, 200);
  early.update(200 + bahn::kAxleSettleMs);
  expect(early.net() == 3, "stale rueckblock does not clear a larger count");

  bahn::AxleCounter otherWay;
  otherWay.onRemote(bahn::Side::B, bahn::MsgType::Achszahl, 3, 0);
  pass(otherWay, bahn::AxleDirection::TowardA, 3, 0);
  expect(otherWay.occupied(bahn::Side::A) && otherWay.countToward(bahn::Side::A) == 3, "occupied toward A");
  expect(otherWay.takeVorblock(bahn::Side::A, count) && count == 3, "vorblock toward A");
  otherWay.update(bahn::kAxleSettleMs);
  expect(otherWay.takeRueckblock(bahn::Side::B, count) && count == 3, "rueckblock goes to the origin");

  bahn::AxleCounter retry;
  pass(retry, bahn::AxleDirection::TowardB, 2, 0);
  expect(retry.takeVorblock(bahn::Side::B, count), "take vorblock");
  retry.onSendFailed(bahn::Side::B, bahn::MsgType::Vorblock);
  expect(retry.takeVorblock(bahn::Side::B, count) && count == 2, "failed vorblock is queued again");
}

}  // namespace

int main() {
  testSensor();
  testCounter();
  if (failures != 0) {
    fprintf(stderr, "%d failed\n", failures);
    return 1;
  }
  printf("ok\n");
  return 0;
}
