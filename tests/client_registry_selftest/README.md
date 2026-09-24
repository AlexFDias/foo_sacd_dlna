# client_registry self-test

Unit test for `client_registry.h` (pure C++, no foobar2000 SDK needed): client accounting (`total = active + idle`), one client owning multiple streams still counting as one active client, idle timeout, and that stream-end counts never go negative on an unmatched end. `./run.sh` needs only g++ (C++17).
