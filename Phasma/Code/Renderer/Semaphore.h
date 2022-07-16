#pragma once

namespace pe
{
    class Semaphore : public IHandle<Semaphore, SemaphoreHandle>
    {
    public:
        Semaphore(bool timeline, const std::string &name);

        ~Semaphore();

        void Wait(uint64_t value);

        void Signal(uint64_t value);

        uint64_t GetValue();

    private:
        bool m_timeline;
    };
}