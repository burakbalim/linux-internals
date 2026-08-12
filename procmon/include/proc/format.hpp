#pragma once

#include <iosfwd>
#include <string>
#include <vector>

namespace proc
{

    std::string human_bytes(long long bytes);
    std::string human_kb(long long kilobytes);
    std::string human_duration(double seconds);
    std::string hex_address(unsigned long long value);

    // Minimal table writer that sizes each column to its widest cell.
    class Table
    {
    public:
        explicit Table(std::vector<std::string> headers);

        void add_row(std::vector<std::string> cells);
        void right_align(size_t column);
        void print(std::ostream &os) const;
        size_t row_count() const { return rows_.size(); }

    private:
        std::vector<std::string> headers_;
        std::vector<std::vector<std::string>> rows_;
        std::vector<bool> right_;
    };

    void print_section(std::ostream &os, const std::string &title);
    void print_field(std::ostream &os, const std::string &label, const std::string &value);

} // namespace proc
