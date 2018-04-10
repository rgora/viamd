#include <cstdarg>
#include <functional>
#include <string>
#include <memory>

// Formats a string like sprintf
// src: http://stackoverflow.com/questions/2342162/stdstring-formatting-like-sprintf
template <typename... Args>
std::string string_format(const std::string& format, Args... args) {
    size_t size = snprintf(nullptr, 0, format.c_str(), args...) + 1;  // Extra space for '\0'
    std::unique_ptr<char[]> buf(new char[size]);
    snprintf(buf.get(), size, format.c_str(), args...);
    return std::string(buf.get(), buf.get() + size - 1);  // We don't want the '\0' inside
}

namespace ImGui {

IMGUI_API int PlotLinesExtended(const char* label, const float* values, int count, int offset = 0,
                                int* selected_idx = NULL, const char* tooltip = NULL,
								std::function<std::string(int, float)> caption_func = nullptr, float scale_min = FLT_MAX,
                                float scale_max = FLT_MAX, ImVec2 graph_size = ImVec2(0, 0),
                                int res = 1, const int* highlighted_indices = nullptr,
                                int highlight_count = 0);

IMGUI_API int PlotHistogramExtended(const char* label, const float* values, int count,
                                    int offset = 0, int* selected_idx = NULL,
                                    const char* tooltip = NULL, std::function<std::string(int, float)> caption_func = nullptr,
                                    float scale_min = FLT_MAX, float scale_max = FLT_MAX,
                                    ImVec2 graph_size = ImVec2(0, 0), int res = 1,
                                    const int* highlighted_indices = nullptr,
                                    int highlight_count = 0);

struct PlotFrame {
    ImGuiID id;
    const char* label;
    int hovered_index;
    int count;
    int offset;
    float scale_min;
    float scale_max;
    ImVec2 frame_bb_min;
    ImVec2 frame_bb_max;
    ImVec2 inner_bb_min;
    ImVec2 inner_bb_max;
    std::string tooltip;
};

enum FillMode_ {
	FillMode_None = 0,
	FillMode_Automatic,
	FillMode_CustomColor
};

enum LineMode_ {
	LineMode_Line = 0,
	LineMode_Histogram
};

struct FrameLineStyle {
	ImU32 line_color = 0xffffffff;
	ImU32 custom_fill_color = 0xffffff44;
	ImU32 fill_mode = FillMode_None;
	ImU32 line_mode = LineMode_Line;
};

// For plotting multiple lines in the same frame!
IMGUI_API PlotFrame BeginPlotFrame(const char* label, ImVec2 size, int offset, int count,
                                   float scale_min, float scale_max, std::function<std::string(int)> x_label_func = nullptr,
                                   const int* highlight_indices = nullptr, int highlight_count = 0);
IMGUI_API int EndPlotFrame(const PlotFrame& frame, int selected_idx = -1);
IMGUI_API void PlotFrameLine(PlotFrame& frame, const char* label, const float* values, FrameLineStyle style = FrameLineStyle(), int selected_idx = -1);

}  // namespace ImGui