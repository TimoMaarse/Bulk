#include <bulk/bulk.hpp>
#include "bulk_test_common.hpp"
#include "set_backend.hpp"

extern environment env;

void test_communication() {
    env.spawn(env.available_processors(), [](auto& world, int s, int p) {
        BULK_SECTION("Put") {
            // test `put` to single variable
            bulk::var<int> a(world);

            bulk::put(world.next_processor(), s, a);
            world.sync();

            BULK_CHECK_ONCE(a.value() == ((s + p - 1) % p),
                            "receive correct value after putting");
        }

        BULK_SECTION("Sugarized put") {
            // test `put` to single variable
            bulk::var<int> a(world);

            a(world.next_processor()) = s;
            world.sync();

            BULK_CHECK_ONCE(a.value() == ((s + p - 1) % p),
                            "receive correct value after sugarized putting");
        }

        BULK_SECTION("Put to self") {
            bulk::var<int> a(world);

            bulk::put(s, s, a);
            world.sync();

            BULK_CHECK_ONCE(a.value() == s,
                            "receive correct value after putting to self");
        }

        BULK_SECTION("Get from self") {
            bulk::var<int> a(world);
            a = s;
            auto b = bulk::get(s, a);
            world.sync();

            BULK_CHECK_ONCE(b.value() == s,
                            "receive correct value after getting from self");
        }

        BULK_SECTION("Put non-int") {
            // test `put` float to single variable
            bulk::var<float> a(world);

            bulk::put(world.next_processor(), 1.0f, a);
            world.sync();

            BULK_CHECK_ONCE(a.value() == 1.0f,
                            "receive correct value after putting float");
        }

        BULK_SECTION("Put multiple") {
            int size = 5;

            // test `put` to multiple variables
            std::vector<bulk::var<int>> xs;
            for (int i = 0; i < size; ++i)
                xs.emplace_back(world);

            for (int i = 0; i < size; ++i) {
                bulk::put(world.next_processor(), s + i, xs[i]);
            }

            world.sync();

            for (int i = 0; i < size; ++i) {
                BULK_CHECK_ONCE(xs[i].value() == ((s + p - 1) % p) + i,
                                "receive correct value after multiple puts to "
                                "array of variables");
            }
        }

        BULK_SECTION("Put unequal") {
            int size = 5;

            // test `put` to multiple variables
            std::vector<bulk::var<int>> xs;
            for (int i = 0; i < size; ++i)
                xs.emplace_back(world);

            if (s == 0)
                for (int i = 1; i < p; ++i) {
                    for (int j = 0; j < size; ++j) {
                        bulk::put(i, i, xs[j]);
                    }
                }

            world.sync();

            bulk::future<int> a(world);
            if (s == 0) a = bulk::get(p - 1, xs[size - 1]);

            world.sync();

            BULK_CHECK_ONCE(
                a.value() == p - 1,
                "receive correct value after heterogeneous puts and getting");
        }

        BULK_SECTION("Get") {
            bulk::var<int> b(world);
            b.value() = s;
            world.sync();

            auto c = bulk::get(world.next_processor(), b);
            world.sync();

            BULK_CHECK_ONCE(c.value() == world.next_processor(),
                            "receive correct value after getting");
        }

        BULK_SECTION("Sugarized get") {
            bulk::var<int> b(world);
            b.value() = s;
            world.sync();

            auto c = b(world.next_processor()).get();
            world.sync();

            BULK_CHECK_ONCE(c.value() == world.next_processor(),
                            "receive correct value after sugarized getting");
        }

        BULK_SECTION("Get multiple") {
            int size = 5;
            bulk::var<int> x(world);
            x.value() = s;

            world.sync();

            std::vector<bulk::future<int>> ys;
            for (int i = 0; i < size; ++i) {
                ys.push_back(bulk::get(world.next_processor(), x));
            }

            world.sync();

            for (auto& y : ys) {
                BULK_CHECK_ONCE(y.value() == world.next_processor(),
                                "receive correct value after getting multiple");
            }
        }

        BULK_SECTION("Coarray") {
            bulk::coarray<int> zs(world, 10);
            zs(world.next_processor())[1] = s;

            world.sync();

            BULK_CHECK_ONCE(
                zs[1] == world.prev_processor(),
                "putting to remote coarray image gives correct result");

            zs[3] = 2;

            BULK_CHECK_ONCE(zs[3] == 2,
                            "writing to local coarray gives correct result");

            auto a = zs(2)[1].get();
            world.sync();

            BULK_CHECK_ONCE(a.value() == 1,
                            "getting from coarray gives correct result");
        }

        BULK_SECTION("Coarray iteration") {
            auto xs = bulk::gather_all(world, s);
            int t = 0;
            for (auto x : xs) {
                BULK_CHECK_ONCE(x == t++, "gather operation succeeded");
            }
        }

        BULK_SECTION("Single message passing") {
            bulk::queue<int, int> q(world);
            q(world.next_processor()).send(123, 1337);
            world.sync();
            for (auto& msg : q) {
                BULK_CHECK_ONCE(msg.tag == 123 && msg.content == 1337,
                                "message passed succesfully");
            }
        }

        BULK_SECTION("Multiple message passing") {
            std::vector<int> contents = {1337, 12345, 1230519, 5, 8};

            bulk::queue<int, int> q(world);
            for (size_t i = 0; i < contents.size(); ++i) {
                q(world.next_processor()).send(s, contents[i]);
            }

            world.sync();

            int k = 0;
            for (auto msg : q) {
                BULK_CHECK_ONCE(msg.tag == world.prev_processor() &&
                                    msg.content == contents[k++],
                                "multiple messages passed succesfully");
            }
        }

        BULK_SECTION("Multiple queue and types message passing") {
            std::vector<int> contents = {1337, 12345, 1230519, 5, 8};
            std::vector<float> contents2 = {1.0f, 2.0f, 3.0f, 4.0f};

            bulk::queue<int, int> q(world);
            bulk::queue<int, float> q2(world);

            for (size_t i = 0; i < contents.size(); ++i) {
                q(world.next_processor()).send(s, contents[i]);
            }
            for (size_t i = 0; i < contents2.size(); ++i) {
                q2(world.next_processor()).send(s, contents2[i]);
            }

            world.sync();

            int k = 0;
            for (auto& msg : q) {
                BULK_CHECK_ONCE(msg.tag == world.prev_processor() &&
                                    msg.content == contents[k++],
                                "received correct result on q");
            }

            int l = 0;
            for (auto& msg : q2) {
                BULK_CHECK_ONCE(msg.tag == world.prev_processor() &&
                                    msg.content == contents2[l++],
                                "received correct result on q2");
            }
        }
    });
}
