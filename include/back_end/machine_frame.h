#pragma once

#include <cstdint>

namespace riscv
{

class MachineFrame
{
public:
    int32_t frame_size() const
    {
        return frame_size_;
    }

    void set_frame_size(int32_t frame_size)
    {
        frame_size_ = frame_size;
    }

    bool has_saved_ra() const
    {
        return has_saved_ra_;
    }

    int32_t ra_offset() const
    {
        return ra_offset_;
    }

    void set_saved_ra(int32_t offset)
    {
        has_saved_ra_ = true;
        ra_offset_ = offset;
    }

    void clear_saved_ra()
    {
        has_saved_ra_ = false;
        ra_offset_ = 0;
    }

private:
    int32_t frame_size_ = 0;
    int32_t ra_offset_ = 0;
    bool has_saved_ra_ = false;
};

} // namespace riscv
