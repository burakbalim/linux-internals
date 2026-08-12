#include "proc/format.hpp"

#include <algorithm>
#include <cstdio>
#include <ostream>

namespace proc
{

    std::string human_bytes(long long bytes)
    {
        if (bytes < 0)
        {
            return "-";
        }
        static const char *units[] = {"B", "K", "M", "G", "T"};
        double value = static_cast<double>(bytes);
        int unit = 0;
        while (value >= 1024.0 && unit < 4)
        {
            value /= 1024.0;
            ++unit;
        }

        char buf[32];
        if (unit == 0)
        {
            std::snprintf(buf, sizeof(buf), "%lld%s", bytes, units[unit]);
        }
        else
        {
            std::snprintf(buf, sizeof(buf), "%.1f%s", value, units[unit]);
        }
        return buf;
    }

    std::string human_kb(long long kilobytes)
    {
        if (kilobytes < 0)
        {
            return "-";
        }
        return human_bytes(kilobytes * 1024);
    }

    std::string human_duration(double seconds)
    {
        char buf[48];
        if (seconds < 60.0)
        {
            std::snprintf(buf, sizeof(buf), "%.2fs", seconds);
        }
        else if (seconds < 3600.0)
        {
            std::snprintf(buf, sizeof(buf), "%dm%02ds", static_cast<int>(seconds) / 60,
                          static_cast<int>(seconds) % 60);
        }
        else
        {
            const int total = static_cast<int>(seconds);
            std::snprintf(buf, sizeof(buf), "%dh%02dm", total / 3600, (total % 3600) / 60);
        }
        return buf;
    }

    std::string hex_address(unsigned long long value)
    {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%012llx", value);
        return buf;
    }

    Table::Table(std::vector<std::string> headers)
        : headers_(std::move(headers)), right_(headers_.size(), false)
    {
    }

    void Table::add_row(std::vector<std::string> cells)
    {
        cells.resize(headers_.size());
        rows_.push_back(std::move(cells));
    }

    void Table::right_align(size_t column)
    {
        if (column < right_.size())
        {
            right_[column] = true;
        }
    }

    void Table::print(std::ostream &os) const
    {
        std::vector<size_t> widths(headers_.size());
        for (size_t i = 0; i < headers_.size(); ++i)
        {
            widths[i] = headers_[i].size();
        }
        for (const auto &row : rows_)
        {
            for (size_t i = 0; i < row.size(); ++i)
            {
                widths[i] = std::max(widths[i], row[i].size());
            }
        }

        const auto emit = [&](const std::vector<std::string> &cells)
        {
            for (size_t i = 0; i < cells.size(); ++i)
            {
                const size_t pad = widths[i] - cells[i].size();
                if (right_[i])
                {
                    os << std::string(pad, ' ') << cells[i];
                }
                else
                {
                    os << cells[i];
                    // Do not pad the trailing column.
                    if (i + 1 < cells.size())
                    {
                        os << std::string(pad, ' ');
                    }
                }
                if (i + 1 < cells.size())
                {
                    os << "  ";
                }
            }
            os << '\n';
        };

        emit(headers_);

        size_t total = 0;
        for (size_t w : widths)
        {
            total += w + 2;
        }
        os << std::string(total > 2 ? total - 2 : 0, '-') << '\n';

        for (const auto &row : rows_)
        {
            emit(row);
        }
    }

    void print_section(std::ostream &os, const std::string &title)
    {
        os << '\n' << title << '\n' << std::string(title.size(), '=') << '\n';
    }

    void print_field(std::ostream &os, const std::string &label, const std::string &value)
    {
        constexpr size_t kLabelWidth = 28;
        os << "  " << label;
        if (label.size() < kLabelWidth)
        {
            os << std::string(kLabelWidth - label.size(), ' ');
        }
        else
        {
            os << ' ';
        }
        os << value << '\n';
    }

} // namespace proc
