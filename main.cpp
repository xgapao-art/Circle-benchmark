#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <d2d1.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <d3d12sdklayers.h>
#include <psapi.h>
#include <dxgi1_4.h>
#include <dxgi1_5.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <cwctype>
#include <fstream>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "user32.lib")

using Microsoft::WRL::ComPtr;

namespace
{
constexpr wchar_t kWindowClassName[] = L"NativeD3D12BenchWindow";
constexpr wchar_t kCanvasClassName[] = L"NativeD3D12CanvasWindow";
constexpr wchar_t kWindowTitle[] = L"Kresleni - benchmark D3D12";
constexpr UINT kPanelWidth = 260;
constexpr int kThicknessSliderId = 101;
constexpr int kColorButtonId = 102;
constexpr int kIntervalEditId = 103;
constexpr int kLineRadioId = 104;
constexpr int kCircleRadioId = 105;
constexpr int kStartButtonId = 106;
constexpr int kClearButtonId = 107;
constexpr int kThicknessLabelId = 108;
constexpr int kModeLabelId = 109;
constexpr int kObjectCountLabelId = 110;
constexpr int kObjectMemoryLabelId = 111;
constexpr int kSpeedLabelId = 112;
constexpr int kPrivateMemoryLabelId = 113;
constexpr int kMemoryBaselineLabelId = 114;
constexpr int kMemoryDeltaLabelId = 115;
constexpr int kHintLabelId = 116;
constexpr int kIntervalLabelId = 117;
constexpr int kRendererLabelId = 118;
constexpr int kVsyncLabelId = 119;
constexpr UINT_PTR kInformationTimerId = 1;
constexpr UINT_PTR kDrawingTimerId = 2;
constexpr UINT kFrameCount = 2;
constexpr UINT kSingleCircleCount = 1;
constexpr UINT kManyCircleCount = 128;
constexpr UINT kSeed = 20261025;
constexpr UINT kCircleSegments = 64;
constexpr float kOuterRadius = 100.0f;
constexpr float kInnerRadius = 90.0f;
constexpr UINT kMinimumSegments = 24;
constexpr UINT kMaximumSegments = 128;
constexpr UINT kGridColumns = 16;
constexpr UINT kGridRows = 8;
constexpr UINT kBenchmarkWarmupFrames = 30;
constexpr UINT kBenchmarkFramesPerRun = 300;
constexpr UINT kBenchmarkRunCount = 5;
constexpr UINT kTimerComparisonWarmupMs = 500;
constexpr UINT kTimerComparisonMeasurementMs = 1500;
constexpr std::array<UINT, 3> kTimerComparisonIntervalsMs = {10, 5, 1};
constexpr UINT kPipelineComparisonWarmupMs = 3000;
constexpr UINT kPipelineComparisonMeasurementMs = 5000;
constexpr std::size_t kPipelineComparisonSampleCapacity = 8192;
constexpr UINT kZeroIntervalBatchSize = 128;
constexpr UINT kMaximumAdaptiveBatchSize = 512;
constexpr UINT kSlowWindowsToEnableBatching = 3;
constexpr UINT kFastWindowsToDisableBatching = 6;
constexpr ULONGLONG kBatchingModeMinimumDurationMs = 5000;
constexpr double kBatchingEnableThreshold = 0.70;
constexpr double kBatchingDisableThreshold = 1.25;

enum class DrawingTimerMode
{
    WindowTimer,
    Qpc,
};

enum class RendererMode
{
    Legacy,
    Shader,
    Direct2D,
};

enum class GpuSelection
{
    Auto,
    Integrated,
    Discrete,
};

enum class TimerComparisonPhase
{
    None,
    Warmup,
    Measuring,
};

enum class PipelineComparisonScenario
{
    RendererIdle,
    CpuOnlyAdd,
    FullD3D12,
};

struct Vertex
{
    float x;
    float y;
    float red;
    float green;
    float blue;
    float alpha;
};

struct CircleParameters
{
    float normalizedCenterX;
    float normalizedCenterY;
    float outerRadius;
    float thickness;
    float red;
    float green;
    float blue;
    UINT segments;
};

struct CircleDraw
{
    UINT indexCount;
    UINT startIndexLocation;
    INT baseVertexLocation;
    float normalizedCenterX;
    float normalizedCenterY;
    float outerRadius;
};

struct DrawingShape
{
    int x1;
    int y1;
    int x2;
    int y2;
    int thickness;
    COLORREF color;
    bool circle;
};

struct RetiredGeometryBuffers
{
    UINT64 fenceValue;
    Microsoft::WRL::ComPtr<ID3D12Resource> vertices;
    Microsoft::WRL::ComPtr<ID3D12Resource> indices;
    Microsoft::WRL::ComPtr<ID3D12Resource> instances;
};

struct CircleInstance
{
    float normalizedCenterX;
    float normalizedCenterY;
    float radius;
    float thickness;
    float red;
    float green;
    float blue;
    float alpha;
};

struct RendererFrameSample
{
    ULONGLONG recordCycles = 0;
    ULONGLONG executeCycles = 0;
    ULONGLONG presentCycles = 0;
    UINT64 recordElapsedTicks = 0;
    UINT64 executeElapsedTicks = 0;
    UINT64 presentElapsedTicks = 0;
    UINT drawCalls = 0;
    UINT renderedCircles = 0;
};

struct Direct2DFrameSample
{
    ULONGLONG drawIssueCycles = 0;
    ULONGLONG endDrawCycles = 0;
    UINT64 drawIssueElapsedTicks = 0;
    UINT64 endDrawElapsedTicks = 0;
    UINT64 frameElapsedTicks = 0;
    UINT drawCalls = 0;
};

struct CircleThroughputMetrics
{
    UINT batchSize = 0;
    UINT64 circlesAddedToScene = 0;
    UINT64 circlesDrawnIntoCanvas = 0;
    UINT64 displayFrames = 0;
    UINT64 canvasUpdates = 0;
    double measurementElapsedMilliseconds = 0.0;
    double sceneDataPreparationMilliseconds = 0.0;
    double sceneAdditionElapsedMilliseconds = 0.0;
    double canvasCompletionElapsedMilliseconds = 0.0;
    double canvasDrawCommandIssueMilliseconds = 0.0;
    double canvasUpdateEndDrawMilliseconds = 0.0;
    double canvasDrawCommandIssueCpuMilliseconds = 0.0;
    double canvasUpdateEndDrawCpuMilliseconds = 0.0;
    double canvasUpdateElapsedMilliseconds = 0.0;
    double rendererGeometryPreparationMilliseconds = 0.0;
    double rendererGeometryUploadMilliseconds = 0.0;
    double rendererCommandRecordMilliseconds = 0.0;
    double rendererCommandExecuteMilliseconds = 0.0;
    double d3dGpuDrawMilliseconds = 0.0;
    double bitmapBlitIssueMilliseconds = 0.0;
    double hwndEndDrawMilliseconds = 0.0;
    double d3dPresentApiMilliseconds = 0.0;
    double d3dFrameFenceWaitMilliseconds = 0.0;
    double d3dFinalFenceDrainMilliseconds = 0.0;
    double circlesAddedPerSecond = 0.0;
    double circlesDrawnIntoCanvasPerSecond = 0.0;
    double endToEndElapsedMilliseconds = 0.0;
    double circlesAddedEndToEndPerSecond = 0.0;
    double circlesDrawnEndToEndPerSecond = 0.0;
    UINT64 canvasDrawCommandIssueCycles = 0;
    UINT64 canvasUpdateEndDrawCycles = 0;
    bool gpuDrawTimeAvailable = false;
    const char* canvasCompletionSemantics = "unknown";
};

struct SampleSummary
{
    double mean = 0.0;
    double median = 0.0;
    double p95 = 0.0;
    double minimum = 0.0;
    double maximum = 0.0;
};

SampleSummary SummarizeSamples(std::vector<double> samples)
{
    SampleSummary summary;
    if (samples.empty())
    {
        return summary;
    }
    std::sort(samples.begin(), samples.end());
    double sum = 0.0;
    for (const double value : samples)
    {
        sum += value;
    }
    summary.mean = sum / static_cast<double>(samples.size());
    const std::size_t middle = samples.size() / 2;
    summary.median = samples.size() % 2 == 0
        ? (samples[middle - 1] + samples[middle]) * 0.5
        : samples[middle];
    const std::size_t p95Index = std::min(
        samples.size() - 1,
        static_cast<std::size_t>(std::ceil(0.95 * samples.size())) - 1);
    summary.p95 = samples[p95Index];
    summary.minimum = samples.front();
    summary.maximum = samples.back();
    return summary;
}

std::uint64_t HashCircleScene(const std::vector<CircleParameters>& circles)
{
    std::uint64_t hash = 14695981039346656037ULL;
    const auto hashBytes = [&hash](const void* data, std::size_t size) {
        const auto* bytes = static_cast<const unsigned char*>(data);
        for (std::size_t index = 0; index < size; ++index)
        {
            hash ^= bytes[index];
            hash *= 1099511628211ULL;
        }
    };
    for (const CircleParameters& circle : circles)
    {
        hashBytes(&circle.normalizedCenterX, sizeof(circle.normalizedCenterX));
        hashBytes(&circle.normalizedCenterY, sizeof(circle.normalizedCenterY));
        hashBytes(&circle.outerRadius, sizeof(circle.outerRadius));
        hashBytes(&circle.thickness, sizeof(circle.thickness));
        hashBytes(&circle.red, sizeof(circle.red));
        hashBytes(&circle.green, sizeof(circle.green));
        hashBytes(&circle.blue, sizeof(circle.blue));
        hashBytes(&circle.segments, sizeof(circle.segments));
    }
    return hash;
}

struct CommandLineOptions
{
    UINT circleCount = 0;
    UINT circleBatchSize = kMaximumAdaptiveBatchSize;
    bool circlesSpecified = false;
    bool benchmark = false;
    bool renderBenchmark = false;
    bool circleThroughputBenchmark = false;
    bool circleBatchSizeSpecified = false;
    bool gpuBenchmark = false;
    bool diagnostics = false;
    bool timerComparison = false;
    bool pipelineComparison = false;
    bool visualCapture = false;
    std::wstring outputFile;
    RendererMode rendererMode = RendererMode::Legacy;
    GpuSelection gpuSelection = GpuSelection::Auto;
    DrawingTimerMode drawingTimerMode = DrawingTimerMode::WindowTimer;
    bool showHelp = false;
};

class DeterministicRng
{
public:
    explicit DeterministicRng(std::uint32_t seed) : state_(seed) {}

    std::uint32_t Next()
    {
        state_ ^= state_ << 13;
        state_ ^= state_ >> 17;
        state_ ^= state_ << 5;
        return state_;
    }

    UINT InRange(UINT minimum, UINT maximum)
    {
        return minimum + Next() % (maximum - minimum + 1);
    }

    INT InRangeSigned(INT minimum, INT maximum)
    {
        return minimum + static_cast<INT>(
            Next() % static_cast<std::uint32_t>(maximum - minimum + 1));
    }

private:
    std::uint32_t state_;
};

void ThrowIfFailed(HRESULT result, const char* operation)
{
    if (FAILED(result))
    {
        std::ostringstream message;
        message << operation << " failed (HRESULT=0x"
                << std::hex << std::uppercase << std::setw(8)
                << std::setfill('0') << static_cast<unsigned long>(result) << ')';
        throw std::runtime_error(message.str());
    }
}
class D3D12App
{
public:
    void SetVisualCaptureEnabled(bool enabled) { visualCaptureEnabled_ = enabled; }
    void SetCircleCount(UINT circleCount)
    {
        if (circleCount != 0 &&
            circleCount != kSingleCircleCount &&
            circleCount != kManyCircleCount &&
            circleCount != 1000 &&
            circleCount != 5000 &&
            circleCount != 10000)
        {
            throw std::runtime_error(
                "Supported circle counts are 1, 128, 1000, 5000, and 10000; use --help.");
        }
        requestedCircleCount_ = circleCount;
    }

    void SetBenchmarkEnabled(bool enabled)
    {
        benchmarkEnabled_ = enabled;
    }

    void SetCircleThroughputBenchmarkEnabled(bool enabled)
    {
        circleThroughputBenchmarkEnabled_ = enabled;
    }

    void SetCircleThroughputBatchSize(UINT batchSize)
    {
        circleThroughputBatchSize_ = batchSize;
    }

    void SetBenchmarkOutputFile(const std::wstring& outputFile)
    {
        benchmarkOutputFile_ = outputFile;
    }

    void WriteBenchmarkOutputFile() const
    {
        if (benchmarkOutputFile_.empty())
        {
            return;
        }
        if (benchmarkOutputJson_.empty())
        {
            throw std::runtime_error(
                "No completed renderer benchmark result is available to write");
        }

        const std::filesystem::path destinationPath(benchmarkOutputFile_);
        std::filesystem::path temporaryPath = destinationPath;
        temporaryPath += L".";
        temporaryPath += std::to_wstring(GetCurrentProcessId());
        temporaryPath += L".tmp";
        std::ofstream output(
            temporaryPath,
            std::ios::binary | std::ios::out | std::ios::trunc);
        if (!output)
        {
            throw std::runtime_error(
                "Could not open the temporary benchmark result file");
        }
        output.write(
            benchmarkOutputJson_.data(),
            static_cast<std::streamsize>(benchmarkOutputJson_.size()));
        output.flush();
        if (!output)
        {
            output.close();
            DeleteFileW(temporaryPath.c_str());
            throw std::runtime_error(
                "Could not write the complete benchmark result file");
        }
        output.close();
        if (output.fail())
        {
            DeleteFileW(temporaryPath.c_str());
            throw std::runtime_error(
                "Could not close the benchmark result file successfully");
        }
        if (!MoveFileExW(
                temporaryPath.c_str(),
                destinationPath.c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            const DWORD error = GetLastError();
            DeleteFileW(temporaryPath.c_str());
            std::ostringstream message;
            message << "Could not publish the benchmark result file "
                    << "(Win32 error " << error << ')';
            throw std::runtime_error(message.str());
        }
    }

    void SetRendererMode(RendererMode mode)
    {
        if (rendererMode_ == mode)
        {
            return;
        }
        rendererMode_ = mode;
        if (interactiveLayout_ && device_)
        {
            RebuildInteractiveGeometry();
            canvasNeedsClear_ = true;
            canvasDirty_ = true;
        }
    }

    void SetGpuSelection(GpuSelection selection)
    {
        gpuSelection_ = selection;
    }

    void SetRenderBenchmarkEnabled(bool enabled)
    {
        renderBenchmarkEnabled_ = enabled;
        if (enabled)
        {
            benchmarkEnabled_ = true;
        }
    }

    void SetPipelineComparisonEnabled(bool enabled)
    {
        pipelineComparisonEnabled_ = enabled;
    }

    void SetDiagnosticsEnabled(bool enabled)
    {
        diagnosticsEnabled_ = enabled;
    }

    void WaitForWindowMessage()
    {
        if (rendererMode_ != RendererMode::Direct2D || !diagnosticsEnabled_)
        {
            WaitMessage();
            return;
        }

        LARGE_INTEGER waitStart{};
        LARGE_INTEGER waitStop{};
        if (!QueryPerformanceCounter(&waitStart))
        {
            throw std::runtime_error(
                "QueryPerformanceCounter failed before WaitMessage");
        }
        if (!WaitMessage())
        {
            throw std::runtime_error("WaitMessage failed");
        }
        if (!QueryPerformanceCounter(&waitStop))
        {
            throw std::runtime_error(
                "QueryPerformanceCounter failed after WaitMessage");
        }
        ++direct2DDiagnosticApplicationWaitCount_;
        direct2DDiagnosticApplicationWaitTicks_ += static_cast<UINT64>(
            waitStop.QuadPart - waitStart.QuadPart);
    }

    void SetDrawingTimerMode(DrawingTimerMode mode)
    {
        drawingTimerMode_ = mode;
    }

    void SetInteractiveLayout(bool enabled)
    {
        interactiveLayout_ = enabled;
    }

    void AttachWindow(HWND window)
    {
        window_ = window;
        canvasWindow_ = window;
    }

    void AttachMainWindow(HWND window)
    {
        mainWindow_ = window;
    }

    void CreateInteractiveControls(HWND parent)
    {
        const INITCOMMONCONTROLSEX commonControls{
            sizeof(INITCOMMONCONTROLSEX),
            ICC_BAR_CLASSES,
        };
        if (!InitCommonControlsEx(&commonControls))
        {
            throw std::runtime_error("InitCommonControlsEx failed");
        }

        font_ = CreateFontW(
            -13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
        if (font_ == nullptr)
        {
            throw std::runtime_error("CreateFontW(panel) failed");
        }

        auto create = [this, parent](int id,
                                     const wchar_t* className,
                                     const wchar_t* text,
                                     DWORD style,
                                     int x,
                                     int y,
                                     int width,
                                     int height,
                                     DWORD extendedStyle = 0) {
            HWND control = CreateWindowExW(
                extendedStyle,
                className,
                text,
                WS_CHILD | WS_VISIBLE | style,
                x,
                y,
                width,
                height,
                parent,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                GetModuleHandleW(nullptr),
                nullptr);
            if (control == nullptr)
            {
                throw std::runtime_error("CreateWindowExW(panel control) failed");
            }
            controls_[static_cast<std::size_t>(id - kThicknessSliderId + 1)] =
                control;
            SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
            return control;
        };
        auto label = [&create](int id, const wchar_t* text, int x, int y,
                               int width, int height) {
            return create(
                id, L"STATIC", text, SS_LEFT | SS_NOPREFIX, x, y, width, height);
        };

        label(kThicknessLabelId, L"Sila cary: 1 px", 16, 12, 228, 22);
        HWND thicknessSlider = create(
            kThicknessSliderId,
            TRACKBAR_CLASSW,
            L"",
            TBS_HORZ | TBS_NOTICKS,
            16,
            38,
            228,
            26);
        SendMessageW(thicknessSlider, TBM_SETRANGE, TRUE, MAKELONG(1, 20));
        SendMessageW(thicknessSlider, TBM_SETPOS, TRUE, 1);

        create(
            kColorButtonId,
            L"BUTTON",
            L"Select color",
            BS_PUSHBUTTON,
            16,
            102,
            228,
            38);
        label(kIntervalLabelId, L"Interval kresleni (ms):", 16, 148, 228, 22);
        HWND intervalEdit = create(
            kIntervalEditId,
            L"EDIT",
            L"100",
            ES_AUTOHSCROLL | ES_NUMBER | WS_BORDER,
            16,
            174,
            228,
            24);
        SendMessageW(intervalEdit, EM_SETLIMITTEXT, 4, 0);

        label(kModeLabelId, L"Tvar nahodneho kresleni:", 16, 202, 228, 20);
        create(
            kLineRadioId,
            L"BUTTON",
            L"Cary",
            BS_AUTORADIOBUTTON | BS_LEFT | WS_GROUP,
            16,
            220,
            88,
            24);
        HWND circleRadio = create(
            kCircleRadioId,
            L"BUTTON",
            L"Kruznice",
            BS_AUTORADIOBUTTON | BS_LEFT,
            112,
            220,
            120,
            24);
        SendMessageW(Control(kLineRadioId), BM_SETCHECK, BST_CHECKED, 0);
        SendMessageW(circleRadio, BM_SETCHECK, BST_UNCHECKED, 0);

        create(
            kStartButtonId,
            L"BUTTON",
            L"Start nahodneho kresleni",
            BS_PUSHBUTTON,
            16,
            250,
            228,
            32);
        label(kRendererLabelId, L"Renderer: Direct3D 12 (HW backend)",
              12, 294, 236, 20);
        label(kVsyncLabelId, L"VSync vypnuta", 12, 316, 236, 20);
        label(kHintLabelId, L"SDL kruznice jsou aproximovane useckami.",
              12, 390, 236, 22);
        label(kObjectCountLabelId, L"Pocet objektu: 0", 12, 418, 236, 20);
        label(kObjectMemoryLabelId, L"Odhad dat objektu (ne proces): 0 B",
              12, 440, 236, 34);
        label(kSpeedLabelId, L"Rychlost: 0 obj/s", 12, 484, 236, 20);
        label(kPrivateMemoryLabelId, L"Soukr. pamet: nedostupna",
              12, 506, 236, 20);
        label(kMemoryBaselineLabelId, L"Zaklad: nedostupny",
              12, 528, 236, 20);
        label(kMemoryDeltaLabelId, L"Zmena od startu: nedostupna",
              12, 550, 236, 20);
        create(
            kClearButtonId,
            L"BUTTON",
            L"Smazat vsechny objekty",
            BS_PUSHBUTTON,
            16,
            560,
            228,
            30);

        RECT parentRect{};
        if (!GetClientRect(parent, &parentRect))
        {
            throw std::runtime_error("GetClientRect(panel parent) failed");
        }
        LayoutInteractiveChildren(
            parentRect.right - parentRect.left,
            parentRect.bottom - parentRect.top);
        UpdateInformationPanel();
    }

    void LayoutInteractiveChildren(int width, int height)
    {
        if (!interactiveLayout_)
        {
            return;
        }
        const int panelWidth = std::min(static_cast<int>(kPanelWidth), width);
        if (canvasWindow_ != nullptr)
        {
            const int canvasWidth = std::max(1, width - static_cast<int>(kPanelWidth));
            if (!SetWindowPos(
                    canvasWindow_,
                    nullptr,
                    static_cast<int>(kPanelWidth),
                    0,
                    canvasWidth,
                    std::max(1, height),
                    SWP_NOACTIVATE | SWP_NOZORDER))
            {
                ReportLayoutFailure(L"SetWindowPos(D3D12 canvas)");
            }
            ShowWindow(
                canvasWindow_,
                width > static_cast<int>(kPanelWidth) ? SW_SHOW : SW_HIDE);
        }
        for (HWND control : controls_)
        {
            if (control != nullptr)
            {
                ShowWindow(
                    control,
                    width > 0 ? SW_SHOW : SW_HIDE);
            }
        }

        const int statsTop = height > 290 ? height - 182 : 154;
        const int clearY = std::max(154, height - 40);
        const int hintY = std::max(352, statsTop - 28);
        const int contentWidth = std::max(0, panelWidth - 32);
        PositionControl(kThicknessLabelId, 16, 12, contentWidth, 22);
        PositionControl(kThicknessSliderId, 16, 38, contentWidth, 26);
        PositionControl(kColorButtonId, 16, 102, contentWidth, 38);
        PositionControl(kIntervalLabelId, 16, 148, contentWidth, 22);
        PositionControl(kIntervalEditId, 16, 174, contentWidth, 24);
        PositionControl(kModeLabelId, 16, 202, contentWidth, 20);
        PositionControl(kLineRadioId, 16, 220, 88, 24);
        PositionControl(kCircleRadioId, 112, 220,
                        std::max(0, panelWidth - 124), 24);
        PositionControl(kStartButtonId, 16, 250, contentWidth, 32);
        PositionControl(kRendererLabelId, 12, 294, panelWidth - 24, 20);
        PositionControl(kVsyncLabelId, 12, 316, panelWidth - 24, 20);
        PositionControl(kHintLabelId, 12, hintY, panelWidth - 24, 22);
        PositionControl(kObjectCountLabelId, 12, statsTop, panelWidth - 24, 20);
        PositionControl(kObjectMemoryLabelId, 12, statsTop + 22,
                        panelWidth - 24, 38);
        PositionControl(kSpeedLabelId, 12, statsTop + 44, panelWidth - 24, 20);
        PositionControl(kPrivateMemoryLabelId, 12, statsTop + 66,
                        panelWidth - 24, 20);
        PositionControl(kMemoryBaselineLabelId, 12, statsTop + 88,
                        panelWidth - 24, 20);
        PositionControl(kMemoryDeltaLabelId, 12, statsTop + 110,
                        panelWidth - 24, 20);
        PositionControl(kClearButtonId, 16, clearY, contentWidth, 30);
    }

    void HandleControlCommand(int id, int notification)
    {
        switch (id)
        {
        case kColorButtonId:
            if (notification == BN_CLICKED)
            {
                ShowColorDialog();
            }
            break;
        case kStartButtonId:
            if (notification == BN_CLICKED)
            {
                ToggleRandomDrawing();
            }
            break;
        case kClearButtonId:
            if (notification == BN_CLICKED)
            {
                ClearDrawing();
                SetWindowTextW(
                    Control(kHintLabelId),
                    L"Plátno vymazáno.");
            }
            break;
        case kLineRadioId:
        case kCircleRadioId:
            if (notification == BN_CLICKED && !randomDrawing_)
            {
                circleMode_ = id == kCircleRadioId;
            }
            break;
        case kIntervalEditId:
            if (notification == EN_KILLFOCUS)
            {
                ValidateIntervalText();
            }
            break;
        default:
            break;
        }
    }

    void HandleHorizontalScroll(HWND control)
    {
        if (control != Control(kThicknessSliderId))
        {
            return;
        }
        thickness_ = static_cast<int>(
            SendMessageW(control, TBM_GETPOS, 0, 0));
        wchar_t label[64]{};
        swprintf_s(label, L"Sila cary: %d px", thickness_);
        SetWindowTextW(Control(kThicknessLabelId), label);
    }

    void HandleInformationTimer()
    {
        UpdateInformationPanel();
    }

    UINT SelectDrawingBatchSize()
    {
        if (drawIntervalMs_ == 0)
        {
            return rendererMode_ == RendererMode::Direct2D
                ? kMaximumAdaptiveBatchSize
                : kZeroIntervalBatchSize;
        }
        if (!batchingEnabled_)
        {
            return 1;
        }

        const ULONGLONG elapsedMs =
            GetTickCount64() - drawingRunStartMs_;
        const UINT64 scheduledShapeCount = elapsedMs / drawIntervalMs_;
        if (scheduledShapeCount <= generatedShapeCountThisRun_)
        {
            return 0;
        }
        return static_cast<UINT>(std::min<UINT64>(
            kMaximumAdaptiveBatchSize,
            scheduledShapeCount - generatedShapeCountThisRun_));
    }

    UINT HandleDrawingTimer()
    {
        if (!randomDrawing_)
        {
            return 0;
        }
        const UINT requestedBatchSize = SelectDrawingBatchSize();
        if (requestedBatchSize == 0)
        {
            return 0;
        }

        const bool collectAddTiming = diagnosticsEnabled_ ||
            timerComparisonPhase_ == TimerComparisonPhase::Measuring;
        LARGE_INTEGER addStart{};
        const bool collectPipelineCycles = pipelineComparisonActive_ &&
            timerComparisonPhase_ == TimerComparisonPhase::Measuring;
        ULONGLONG addCycleStart = 0;
        if (collectPipelineCycles &&
            !QueryThreadCycleTime(GetCurrentThread(), &addCycleStart))
        {
            throw std::runtime_error("QueryThreadCycleTime failed before object addition");
        }
        if (collectAddTiming && !QueryPerformanceCounter(&addStart))
        {
            throw std::runtime_error(
                "QueryPerformanceCounter failed before object addition");
        }
        const std::size_t firstShapeIndex = drawingShapes_.size();
        UINT addedCount = 0;
        for (; addedCount < requestedBatchSize; ++addedCount)
        {
            if (!GenerateRandomShape())
            {
                break;
            }
        }
        if (addedCount != 0)
        {
            OnInteractiveShapesAdded(firstShapeIndex, addedCount);
        }
        LARGE_INTEGER addStop{};
        if (collectAddTiming && !QueryPerformanceCounter(&addStop))
        {
            throw std::runtime_error(
                "QueryPerformanceCounter failed after object addition");
        }
        ULONGLONG addCycleStop = 0;
        if (collectPipelineCycles &&
            !QueryThreadCycleTime(GetCurrentThread(), &addCycleStop))
        {
            throw std::runtime_error("QueryThreadCycleTime failed after object addition");
        }
        if (addedCount != 0)
        {
            generatedInSpeedWindow_ += addedCount;
            generatedShapeCountThisRun_ += addedCount;
            if (interactiveLayout_ && interactiveGeometryEnabled_)
            {
                pendingGeneratedShapesForPresent_ += addedCount;
            }
            if (diagnosticsEnabled_)
            {
                diagnosticObjectCount_ += addedCount;
                diagnosticAddTicks_ += static_cast<UINT64>(
                    addStop.QuadPart - addStart.QuadPart);
            }
            if (timerComparisonPhase_ == TimerComparisonPhase::Measuring &&
                addStop.QuadPart >= timerComparisonMeasurementStart_.QuadPart &&
                addStop.QuadPart < timerComparisonMeasurementEnd_.QuadPart)
            {
                for (UINT index = 0; index < addedCount; ++index)
                {
                    RecordTimerComparisonAddition(addStop.QuadPart);
                }
                if (pipelineComparisonActive_)
                {
                    const LONGLONG objectElapsedTicks = addStop.QuadPart - addStart.QuadPart;
                    pipelineObjectElapsedTicks_.push_back(objectElapsedTicks);
                    pipelineObjectElapsedTickSum_ += static_cast<UINT64>(objectElapsedTicks);
                    const ULONGLONG objectCycles = addCycleStop - addCycleStart;
                    pipelineObjectCycles_.push_back(objectCycles);
                    pipelineObjectCycleSum_ += objectCycles;
                }
            }
        }
        return addedCount;
    }

    void UpdateDrawingRateWindow()
    {
        const ULONGLONG now = GetTickCount64();
        const ULONGLONG elapsedMs = now - speedWindowStartMs_;
        if (elapsedMs < 1000)
        {
            return;
        }

        const UINT64 generatedAndPresentedCount = std::min(
            generatedInSpeedWindow_, renderedInSpeedWindow_);
        const double actualRate =
            static_cast<double>(generatedAndPresentedCount) * 1000.0 /
            static_cast<double>(elapsedMs);
        actualObjectsPerSecond_ = static_cast<UINT>(std::min(
            actualRate,
            static_cast<double>(std::numeric_limits<UINT>::max())));

        if (randomDrawing_ && drawIntervalMs_ != 0 &&
            !timerComparisonActive_ && !pipelineComparisonActive_)
        {
            const double targetRate =
                1000.0 / static_cast<double>(drawIntervalMs_);
            const bool canChangeBatching =
                !hasBatchingModeChanged_ ||
                now - lastBatchingModeChangeMs_ >=
                    kBatchingModeMinimumDurationMs;
            if (!batchingEnabled_)
            {
                fastWindowCount_ = 0;
                if (actualRate < targetRate * kBatchingEnableThreshold)
                {
                    slowWindowCount_ = std::min(
                        slowWindowCount_ + 1,
                        kSlowWindowsToEnableBatching);
                }
                else
                {
                    slowWindowCount_ = 0;
                }
                if (slowWindowCount_ >= kSlowWindowsToEnableBatching &&
                    canChangeBatching)
                {
                    batchingEnabled_ = true;
                    hasBatchingModeChanged_ = true;
                    lastBatchingModeChangeMs_ = now;
                    slowWindowCount_ = 0;
                    if (diagnosticsEnabled_)
                    {
                        std::cout << "[diag] adaptive batching enabled\n"
                                  << std::flush;
                    }
                }
            }
            else
            {
                slowWindowCount_ = 0;
                if (actualRate >= targetRate * kBatchingDisableThreshold)
                {
                    fastWindowCount_ = std::min(
                        fastWindowCount_ + 1,
                        kFastWindowsToDisableBatching);
                }
                else
                {
                    fastWindowCount_ = 0;
                }
                if (fastWindowCount_ >= kFastWindowsToDisableBatching &&
                    canChangeBatching)
                {
                    batchingEnabled_ = false;
                    hasBatchingModeChanged_ = true;
                    lastBatchingModeChangeMs_ = now;
                    fastWindowCount_ = 0;
                    if (diagnosticsEnabled_)
                    {
                        std::cout << "[diag] adaptive batching disabled\n"
                                  << std::flush;
                    }
                }
            }
        }

        generatedInSpeedWindow_ = 0;
        renderedInSpeedWindow_ = 0;
        speedWindowStartMs_ = now;
        UpdateInformationPanel();
    }

    void HandleWindowDrawingTimer()
    {
        if (drawingTimerMode_ == DrawingTimerMode::WindowTimer)
        {
            HandleDrawingTimer();
        }
    }

    void PumpQpcDrawingTimer()
    {
        if (!randomDrawing_ || drawingTimerMode_ != DrawingTimerMode::Qpc)
        {
            return;
        }

        LARGE_INTEGER now{};
        if (!QueryPerformanceCounter(&now))
        {
            throw std::runtime_error(
                "QueryPerformanceCounter failed in QPC drawing mode");
        }
        if (now.QuadPart < nextDrawingDeadline_.QuadPart)
        {
            return;
        }

        HandleDrawingTimer();
        if (!QueryPerformanceCounter(&now))
        {
            throw std::runtime_error(
                "QueryPerformanceCounter failed after QPC drawing tick");
        }
        if (drawIntervalMs_ == 0)
        {
            nextDrawingDeadline_.QuadPart = now.QuadPart + 1;
        }
        else if (batchingEnabled_)
        {
            const UINT64 nextScheduledShape = generatedShapeCountThisRun_ + 1;
            const UINT64 intervalTicks =
                static_cast<UINT64>(drawingIntervalTicks_);
            const UINT64 maximumOffset = static_cast<UINT64>(
                std::numeric_limits<LONGLONG>::max() -
                drawingStartQpc_.QuadPart);
            if (nextScheduledShape > maximumOffset / intervalTicks)
            {
                throw std::overflow_error(
                    "Adaptive QPC drawing deadline exceeds counter range");
            }
            nextDrawingDeadline_.QuadPart =
                drawingStartQpc_.QuadPart +
                static_cast<LONGLONG>(nextScheduledShape * intervalTicks);
        }
        else
        {
            nextDrawingDeadline_.QuadPart += drawingIntervalTicks_;
            while (nextDrawingDeadline_.QuadPart <= now.QuadPart)
            {
                nextDrawingDeadline_.QuadPart += drawingIntervalTicks_;
                ++qpcSkippedDeadlines_;
            }
        }
    }

    void BeginTimerComparison()
    {
        if (!QueryPerformanceFrequency(&timerComparisonFrequency_) ||
            timerComparisonFrequency_.QuadPart <= 0)
        {
            throw std::runtime_error(
                "QueryPerformanceFrequency failed for timer comparison");
        }
        timerComparisonActive_ = true;
        timerComparisonCaseIndex_ = 0;
        StartTimerComparisonCase();
    }

    void BeginPipelineComparison()
    {
        if (!QueryPerformanceFrequency(&timerComparisonFrequency_) ||
            timerComparisonFrequency_.QuadPart <= 0)
        {
            throw std::runtime_error(
                "QueryPerformanceFrequency failed for pipeline comparison");
        }
        timerComparisonIntervalsTicks_.reserve(kPipelineComparisonSampleCapacity);
        pipelineObjectElapsedTicks_.reserve(kPipelineComparisonSampleCapacity);
        pipelineObjectCycles_.reserve(kPipelineComparisonSampleCapacity);
        pipelineComparisonActive_ = true;
        timerComparisonActive_ = true;
        timerComparisonCaseIndex_ = 0;
        StartTimerComparisonCase();
    }

    void UpdateTimerComparison()
    {
        if (!timerComparisonActive_)
        {
            return;
        }

        LARGE_INTEGER now{};
        if (!QueryPerformanceCounter(&now))
        {
            throw std::runtime_error(
                "QueryPerformanceCounter failed in timer comparison");
        }
        if (timerComparisonPhase_ == TimerComparisonPhase::Warmup &&
            now.QuadPart >= timerComparisonPhaseDeadline_.QuadPart)
        {
            StopRandomDrawing();
            ClearDrawing();
            randomGenerator_.seed(kSeed);
            timerComparisonIntervalsTicks_.clear();
            pipelineObjectElapsedTicks_.clear();
            pipelineObjectElapsedTickSum_ = 0;
            pipelineObjectCycles_.clear();
            pipelineObjectCycleSum_ = 0;
            pipelineGeometryPrepTicks_ = 0;
            pipelineGeometryUploadTicks_ = 0;
            timerComparisonAddedCount_ = 0;
            timerComparisonLastAdditionTicks_ = 0;
            qpcSkippedDeadlines_ = 0;
            pipelinePresentCount_ = 0;
            gpuTimestampTicksTotal_ = 0;
            gpuTimestampFrameCount_ = 0;
            timerComparisonPhase_ = TimerComparisonPhase::Measuring;
            if ((!pipelineComparisonActive_ ||
                 pipelineScenario_ != PipelineComparisonScenario::RendererIdle) &&
                !StartRandomDrawing())
            {
                throw std::runtime_error(
                    "Could not start timer comparison measurement");
            }
            if (!QueryPerformanceCounter(&timerComparisonMeasurementStart_))
            {
                throw std::runtime_error(
                    "QueryPerformanceCounter failed at measurement start");
            }
            if (pipelineComparisonActive_ &&
                !GetThreadTimes(
                    GetCurrentThread(), &pipelineCpuStartCreation_,
                    &pipelineCpuStartExit_, &pipelineCpuStartKernel_,
                    &pipelineCpuStartUser_))
            {
                throw std::runtime_error("GetThreadTimes failed at pipeline start");
            }
            if (pipelineComparisonActive_ &&
                !QueryThreadCycleTime(
                    GetCurrentThread(), &pipelineThreadCyclesStart_))
            {
                throw std::runtime_error("QueryThreadCycleTime failed at pipeline start");
            }
            timerComparisonMeasurementEnd_.QuadPart =
                timerComparisonMeasurementStart_.QuadPart +
                static_cast<LONGLONG>(
                    timerComparisonFrequency_.QuadPart *
                    (pipelineComparisonActive_
                        ? kPipelineComparisonMeasurementMs
                        : kTimerComparisonMeasurementMs) / 1000);
            timerComparisonPhaseDeadline_ = timerComparisonMeasurementEnd_;
            std::cout << (pipelineComparisonActive_ ? "[pipeline-test] measuring " : "[timer-test] measuring ")
                      << (pipelineComparisonActive_ ? PipelineScenarioName(pipelineScenario_) : TimerModeName(drawingTimerMode_))
                      << " target=" << drawIntervalMs_ << " ms for "
                      << (pipelineComparisonActive_ ? kPipelineComparisonMeasurementMs : kTimerComparisonMeasurementMs)
                      << " ms\n";
            return;
        }

        if (timerComparisonPhase_ == TimerComparisonPhase::Measuring &&
            now.QuadPart >= timerComparisonPhaseDeadline_.QuadPart)
        {
            if (pipelineComparisonActive_)
            {
                if (!GetThreadTimes(
                        GetCurrentThread(), &pipelineCpuEndCreation_,
                        &pipelineCpuEndExit_, &pipelineCpuEndKernel_,
                        &pipelineCpuEndUser_))
                {
                    throw std::runtime_error("GetThreadTimes failed at pipeline end");
                }
                if (!QueryThreadCycleTime(
                        GetCurrentThread(), &pipelineThreadCyclesEnd_))
                {
                    throw std::runtime_error("QueryThreadCycleTime failed at pipeline end");
                }
                WaitForGpu();
            }
            StopRandomDrawing();
            if (pipelineComparisonActive_)
            {
                PrintPipelineComparisonResult();
            }
            else
            {
                PrintTimerComparisonResult();
            }
            ++timerComparisonCaseIndex_;
            const std::size_t caseCount = pipelineComparisonActive_ ? 9 : 6;
            if (timerComparisonCaseIndex_ >= caseCount)
            {
                timerComparisonActive_ = false;
                timerComparisonPhase_ = TimerComparisonPhase::None;
                if (pipelineComparisonActive_)
                {
                    pipelineComparisonActive_ = false;
                    interactiveGeometryEnabled_ = true;
                    drawingTimerMode_ = DrawingTimerMode::WindowTimer;
                    drawIntervalMs_ = 100;
                    circleMode_ = false;
                    SendMessageW(Control(kLineRadioId), BM_SETCHECK, BST_CHECKED, 0);
                    SendMessageW(Control(kCircleRadioId), BM_SETCHECK, BST_UNCHECKED, 0);
                    SetWindowTextW(Control(kIntervalEditId), L"100");
                    ClearDrawing();
                    SetWindowTextW(Control(kHintLabelId), L"Srovnávací měření dokončeno.");
                }
                else
                {
                    PostMessageW(mainWindow_, WM_CLOSE, 0, 0);
                }
                return;
            }
            StartTimerComparisonCase();
        }
    }

    void HandleCanvasClick(int x, int y)
    {
        if (requestedCircleCount_ != 0 || randomDrawing_)
        {
            return;
        }
        if (!pendingPoint_)
        {
            pendingX_ = x;
            pendingY_ = y;
            pendingThickness_ = thickness_;
            pendingColor_ = selectedColor_;
            pendingPoint_ = true;
            return;
        }
        drawingShapes_.push_back({
            pendingX_, pendingY_, x, y,
            pendingThickness_, pendingColor_, false});
        pendingPoint_ = false;
        SetWindowTextW(
            mainWindow_,
            (std::wstring(kWindowTitle) + L" - Kliknuti: X=" +
             std::to_wstring(x) + L", Y=" + std::to_wstring(y)).c_str());
        OnInteractiveShapeAdded();
    }

    void LayoutFromParent(UINT width, UINT height)
    {
        LayoutInteractiveChildren(static_cast<int>(width), static_cast<int>(height));
    }

    void OnResize(UINT width, UINT height)
    {
        if (width == 0 || height == 0)
        {
            minimized_ = true;
            return;
        }

        minimized_ = false;
        pendingWidth_ = width;
        pendingHeight_ = height;
        resizePending_ = true;
    }

    void Initialize()
    {
        RECT clientRect{};
        if (!GetClientRect(window_, &clientRect))
        {
            throw std::runtime_error("GetClientRect failed");
        }

        width_ = static_cast<UINT>(clientRect.right - clientRect.left);
        height_ = static_cast<UINT>(clientRect.bottom - clientRect.top);
        UpdateCanvasWidth();
        if (width_ == 0 || height_ == 0)
        {
            throw std::runtime_error("The initial client area is empty");
        }

        if (diagnosticsEnabled_)
        {
            if (!QueryPerformanceFrequency(&diagnosticFrequency_) ||
                diagnosticFrequency_.QuadPart <= 0)
            {
                throw std::runtime_error(
                    "QueryPerformanceFrequency failed for diagnostics");
            }
            diagnosticWindowStartMs_ = GetTickCount64();
        }

        LARGE_INTEGER scenePreparationStart{};
        LARGE_INTEGER scenePreparationStop{};
        const bool measureScenePreparation =
            rendererMode_ == RendererMode::Direct2D ||
            circleThroughputBenchmarkEnabled_;
        if (measureScenePreparation &&
            !QueryPerformanceCounter(&scenePreparationStart))
        {
            throw std::runtime_error(
                "QueryPerformanceCounter failed before scene preparation");
        }
        if (visualCaptureEnabled_)
        {
            GenerateVisualCaptureScene();
        }
        else
        {
            GenerateCircleParameters();
        }
        if (circleThroughputBenchmarkEnabled_)
        {
            circleThroughputTargetCount_ = requestedCircleCount_;
            circleThroughputSceneHash_ = HashCircleScene(circleParameters_);
            CreateCircleThroughputDrawingShapes();
            circleParameters_.clear();
            requestedCircleCount_ = 0;
        }
        if (measureScenePreparation)
        {
            if (!QueryPerformanceCounter(&scenePreparationStop))
            {
                throw std::runtime_error(
                    "QueryPerformanceCounter failed after scene preparation");
            }
            const UINT64 preparationTicks = static_cast<UINT64>(
                scenePreparationStop.QuadPart - scenePreparationStart.QuadPart);
            if (rendererMode_ == RendererMode::Direct2D)
            {
                direct2DScenePreparationTicks_ = preparationTicks;
            }
            if (circleThroughputBenchmarkEnabled_)
            {
                circleThroughputScenePreparationTicks_ = preparationTicks;
            }
        }
        if (rendererMode_ == RendererMode::Direct2D)
        {
            CreateDirect2DResources();
        }
        else
        {
            if (rendererMode_ == RendererMode::Legacy)
            {
                GenerateCircleGeometry();
            }
            else
            {
                GenerateShaderCircleInstances();
            }
            CreateDeviceAndQueue();
            CreateSwapChain();
            CreateRenderTargetViews();
            if (visualCaptureEnabled_)
            {
                CreateVisualReadback();
            }
            if (interactiveLayout_)
            {
                CreatePersistentCanvas();
            }
            CreateCommandObjects();
            CreateCirclePipeline();
            if (rendererMode_ == RendererMode::Shader)
            {
                CreateProceduralCirclePipeline();
            }
            if (rendererMode_ == RendererMode::Legacy)
            {
                UploadCircleBuffers();
            }
            else
            {
                UploadShaderInstanceBuffer();
            }
            if (benchmarkEnabled_ || pipelineComparisonEnabled_)
            {
                CreateGpuTimestampQueries();
            }

            fenceEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
            if (fenceEvent_ == nullptr)
            {
                throw std::runtime_error("CreateEventW failed");
            }
        }

        std::wstring title = kWindowTitle;
        if (requestedCircleCount_ != 0)
        {
            title += L" - ";
            title += std::to_wstring(circleParameters_.size());
            title += L" circle(s)";
        }
        if (!SetWindowTextW(mainWindow_, title.c_str()))
        {
            throw std::runtime_error("SetWindowTextW failed");
        }
        if (diagnosticsEnabled_)
        {
            PrintRendererDiagnostics();
        }
        UpdateInformationPanel();
    }

    void RunVisualCapture(const std::wstring& outputPath)
    {
        if (!visualCaptureEnabled_)
        {
            throw std::runtime_error("Visual capture is not enabled");
        }
        Render();
        WaitForGpu();
        SaveVisualReadback(outputPath);
        std::wcout << L"[visual-capture] renderer="
                   << (rendererMode_ == RendererMode::Legacy ? L"legacy" : L"shader")
                   << L" size=" << width_ << L"x" << height_
                   << L" circles=" << circleParameters_.size()
                   << L" scene_hash=0x" << std::hex << HashCircleScene(circleParameters_)
                   << std::dec << L"\n";
        for (std::size_t index = 0; index < circleParameters_.size(); ++index)
        {
            const CircleParameters& circle = circleParameters_[index];
            std::wcout << L"[visual-capture] circle=" << index
                       << L" center=" << circle.normalizedCenterX << L"," << circle.normalizedCenterY
                       << L" radius=" << circle.outerRadius
                       << L" thickness=" << circle.thickness
                       << L" color=" << circle.red << L"," << circle.green << L"," << circle.blue
                       << L" segments=" << circle.segments << L"\n";
        }
        std::wcout << L"[visual-capture] output=" << outputPath << L"\n";
    }

    void Render()
    {
        if (minimized_)
        {
            return;
        }

        if (resizePending_)
        {
            ApplyResize();
        }
        if (rendererMode_ == RendererMode::Direct2D)
        {
            RenderDirect2DFrame();
            return;
        }

        LARGE_INTEGER frameStart{};
        LARGE_INTEGER throughputWaitStart{};
        if ((diagnosticsEnabled_ &&
             !QueryPerformanceCounter(&frameStart)) ||
            (circleThroughputBenchmarkActive_ &&
             !QueryPerformanceCounter(&throughputWaitStart)))
        {
            throw std::runtime_error(
                "QueryPerformanceCounter failed before frame wait");
        }

        const UINT frameIndex = swapChain_->GetCurrentBackBufferIndex();
        WaitForFrame(frameIndex);
        LARGE_INTEGER throughputWaitStop{};
        if (circleThroughputBenchmarkActive_)
        {
            if (!QueryPerformanceCounter(&throughputWaitStop))
            {
                throw std::runtime_error(
                    "QueryPerformanceCounter failed after D3D12 frame wait");
            }
            AddCircleThroughputElapsed(
                circleThroughputMetrics_.d3dFrameFenceWaitMilliseconds,
                static_cast<UINT64>(
                    throughputWaitStop.QuadPart -
                    throughputWaitStart.QuadPart));
        }
        LARGE_INTEGER waitEnd{};
        if (diagnosticsEnabled_ && !QueryPerformanceCounter(&waitEnd))
        {
            throw std::runtime_error(
                "QueryPerformanceCounter failed after frame wait");
        }

        RecordAndSubmitFrame(frameIndex);
        commandsSubmitted_ = true;
        if (rendererBenchmarkActive_)
        {
            const UINT expectedDrawCalls =
                rendererMode_ == RendererMode::Shader
                    ? (requestedCircleCount_ == 0 ? 0U : 1U)
                    : static_cast<UINT>(circleDraws_.size());
            if (lastFrameRenderedCircleCount_ != requestedCircleCount_ ||
                lastFrameDrawCallCount_ != expectedDrawCalls)
            {
                throw std::runtime_error(
                    "Renderer benchmark submitted an unexpected circle or draw count");
            }
        }
        LARGE_INTEGER recordEnd{};
        if (diagnosticsEnabled_ && !QueryPerformanceCounter(&recordEnd))
        {
            throw std::runtime_error(
                "QueryPerformanceCounter failed after command submission");
        }

        LARGE_INTEGER benchmarkPresentStart{};
        LARGE_INTEGER presentReturn{};
        ULONGLONG benchmarkPresentCycleStart = 0;
        ULONGLONG benchmarkPresentCycleStop = 0;
        const bool measurePresent =
            rendererBenchmarkActive_ || circleThroughputBenchmarkActive_;
        if (measurePresent &&
            (!QueryPerformanceCounter(&benchmarkPresentStart) ||
             !QueryThreadCycleTime(
                 GetCurrentThread(), &benchmarkPresentCycleStart)))
        {
            throw std::runtime_error("Could not start Present timing");
        }
        const UINT presentFlags =
            tearingSupported_ ? DXGI_PRESENT_ALLOW_TEARING : 0;
        ThrowIfFailed(
            swapChain_->Present(0, presentFlags),
            "IDXGISwapChain::Present");
        if (measurePresent)
        {
            if (!QueryThreadCycleTime(
                    GetCurrentThread(), &benchmarkPresentCycleStop) ||
                !QueryPerformanceCounter(&presentReturn))
            {
                throw std::runtime_error("Could not stop Present timing");
            }
        }
        if (rendererBenchmarkActive_)
        {
            if (presentReturn.QuadPart < rendererBenchmarkMeasurementEnd_.QuadPart)
            {
                ++rendererBenchmarkPresentReturns_;
                rendererBenchmarkDrawCalls_ += lastFrameDrawCallCount_;
                rendererBenchmarkRenderedCircles_ += lastFrameRenderedCircleCount_;
                RendererFrameSample sample = lastRendererFrameSample_;
                sample.presentCycles =
                    benchmarkPresentCycleStop - benchmarkPresentCycleStart;
                sample.presentElapsedTicks = static_cast<UINT64>(
                    presentReturn.QuadPart - benchmarkPresentStart.QuadPart);
                sample.drawCalls = lastFrameDrawCallCount_;
                sample.renderedCircles = lastFrameRenderedCircleCount_;
                rendererBenchmarkFrameSamples_.push_back(sample);
            }
        }
        if (circleThroughputBenchmarkActive_)
        {
            ++circleThroughputMetrics_.displayFrames;
            if (lastFrameRenderedCircleCount_ != 0)
            {
                ++circleThroughputMetrics_.canvasUpdates;
                circleThroughputMetrics_.circlesDrawnIntoCanvas +=
                    lastFrameRenderedCircleCount_;
            }
            AddCircleThroughputElapsed(
                circleThroughputMetrics_.d3dPresentApiMilliseconds,
                static_cast<UINT64>(
                    presentReturn.QuadPart - benchmarkPresentStart.QuadPart));
        }
        if (pipelineComparisonActive_ &&
            timerComparisonPhase_ == TimerComparisonPhase::Measuring)
        {
            if (presentReturn.QuadPart == 0 &&
                !QueryPerformanceCounter(&presentReturn))
            {
                throw std::runtime_error("QueryPerformanceCounter failed after Present");
            }
            if (presentReturn.QuadPart >= timerComparisonMeasurementStart_.QuadPart &&
                presentReturn.QuadPart < timerComparisonMeasurementEnd_.QuadPart)
            {
                ++pipelinePresentCount_;
            }
        }
        LARGE_INTEGER presentEnd{};
        if (diagnosticsEnabled_ && !QueryPerformanceCounter(&presentEnd))
        {
            throw std::runtime_error(
                "QueryPerformanceCounter failed after Present");
        }
        SignalFrame(frameIndex);
        LARGE_INTEGER signalEnd{};
        if (diagnosticsEnabled_)
        {
            if (!QueryPerformanceCounter(&signalEnd))
            {
                throw std::runtime_error(
                    "QueryPerformanceCounter failed after fence signal");
            }
            diagnosticFrameCount_++;
            diagnosticFenceWaitTicks_ += static_cast<UINT64>(
                waitEnd.QuadPart - frameStart.QuadPart);
            diagnosticRecordTicks_ += static_cast<UINT64>(
                recordEnd.QuadPart - waitEnd.QuadPart);
            diagnosticPresentTicks_ += static_cast<UINT64>(
                presentEnd.QuadPart - recordEnd.QuadPart);
            diagnosticSignalTicks_ += static_cast<UINT64>(
                signalEnd.QuadPart - presentEnd.QuadPart);
            ReportDiagnosticsIfDue();
        }
        renderedInSpeedWindow_ += pendingGeneratedShapesForPresent_;
        pendingGeneratedShapesForPresent_ = 0;
        UpdateDrawingRateWindow();
    }

    void ReportDiagnosticsIfDue()
    {
        const ULONGLONG now = GetTickCount64();
        const ULONGLONG elapsedMs = now - diagnosticWindowStartMs_;
        if (elapsedMs < 1000)
        {
            return;
        }

        const double elapsedSeconds = static_cast<double>(elapsedMs) / 1000.0;
        const double ticksToMs = 1000.0 /
            static_cast<double>(diagnosticFrequency_.QuadPart);
        const double frameCount = static_cast<double>(diagnosticFrameCount_);
        const double objectCount = static_cast<double>(diagnosticObjectCount_);
        std::cout << std::fixed << std::setprecision(2)
                  << "[diag] Present returns/s="
                  << frameCount / elapsedSeconds
                  << ", objects added/s=" << objectCount / elapsedSeconds
                  << ", add CPU ms/object="
                  << (diagnosticObjectCount_ == 0
                          ? 0.0
                          : static_cast<double>(diagnosticAddTicks_) *
                                ticksToMs / objectCount)
                  << ", frame fence wait ms/frame="
                  << (frameCount == 0.0
                          ? 0.0
                          : static_cast<double>(diagnosticFenceWaitTicks_) *
                                ticksToMs / frameCount)
                  << ", record+execute ms/frame="
                  << (frameCount == 0.0
                          ? 0.0
                          : static_cast<double>(diagnosticRecordTicks_) *
                                ticksToMs / frameCount)
                  << ", Present CPU ms/call="
                  << (frameCount == 0.0
                          ? 0.0
                          : static_cast<double>(diagnosticPresentTicks_) *
                                ticksToMs / frameCount)
                  << ", fence signal ms/frame="
                  << (frameCount == 0.0
                          ? 0.0
                          : static_cast<double>(diagnosticSignalTicks_) *
                                ticksToMs / frameCount)
                  << '\n' << std::flush;
        if (rendererMode_ == RendererMode::Legacy)
        {
            PrintLegacyMemoryDiagnostics();
        }

        diagnosticWindowStartMs_ = now;
        diagnosticFrameCount_ = 0;
        diagnosticObjectCount_ = 0;
        diagnosticAddTicks_ = 0;
        diagnosticFenceWaitTicks_ = 0;
        diagnosticRecordTicks_ = 0;
        diagnosticPresentTicks_ = 0;
        diagnosticSignalTicks_ = 0;
    }

    void RunBenchmark()
    {
        LARGE_INTEGER frequency{};
        if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0)
        {
            throw std::runtime_error(
                "QueryPerformanceFrequency failed or returned an invalid frequency");
        }

        std::cout << "CPU command-submission benchmark\n"
                  << "Circles/frame: " << circleDraws_.size() << '\n'
                  << "D3D12 debug layer: "
                  << (debugLayerEnabled_ ? "enabled" : "unavailable") << '\n'
                  << "Warm-up frames: " << kBenchmarkWarmupFrames << '\n'
                  << "Measured runs: " << kBenchmarkRunCount
                  << " x " << kBenchmarkFramesPerRun << " frames\n"
                  << "Timer covers command-list reset/record/close and "
                     "ExecuteCommandLists; excludes GPU completion waits, "
                     "queue fence Signal, Present, and initialization.\n"
                  << "GPU time is measured separately with timestamp queries.\n";

        UINT frameIndex = 0;
        for (UINT frame = 0; frame < kBenchmarkWarmupFrames; ++frame)
        {
            WaitForFrame(frameIndex);
            RecordAndSubmitFrame(frameIndex);
            commandsSubmitted_ = true;
            SignalFrame(frameIndex);
            frameIndex = (frameIndex + 1) % kFrameCount;
        }
        WaitForGpu();
        gpuTimestampTicksTotal_ = 0;
        gpuTimestampFrameCount_ = 0;

        std::vector<double> runMilliseconds;
        runMilliseconds.reserve(kBenchmarkRunCount);
        std::uint64_t submittedFrames = 0;
        for (UINT run = 0; run < kBenchmarkRunCount; ++run)
        {
            LONGLONG measuredTicks = 0;
            UINT64 drawCalls = 0;
            gpuTimestampTicksTotal_ = 0;
            gpuTimestampFrameCount_ = 0;
            for (UINT frame = 0; frame < kBenchmarkFramesPerRun; ++frame)
            {
                WaitForFrame(frameIndex);
                LARGE_INTEGER start{};
                LARGE_INTEGER stop{};
                if (!QueryPerformanceCounter(&start))
                {
                    throw std::runtime_error(
                        "QueryPerformanceCounter failed before frame submission");
                }
                RecordAndSubmitFrame(frameIndex);
                drawCalls += lastFrameDrawCallCount_;
                if (!QueryPerformanceCounter(&stop))
                {
                    throw std::runtime_error(
                        "QueryPerformanceCounter failed after frame submission");
                }
                measuredTicks += stop.QuadPart - start.QuadPart;
                ++submittedFrames;
                commandsSubmitted_ = true;
                SignalFrame(frameIndex);
                frameIndex = (frameIndex + 1) % kFrameCount;
            }

            WaitForGpu();
            const double milliseconds =
                static_cast<double>(measuredTicks) * 1000.0 /
                static_cast<double>(frequency.QuadPart);
            runMilliseconds.push_back(milliseconds);
            std::cout << "Run " << (run + 1) << ": "
                      << std::fixed << std::setprecision(3)
                      << milliseconds << " ms total, "
                      << milliseconds / kBenchmarkFramesPerRun
                      << " ms/frame, " << kBenchmarkFramesPerRun
                      << " submitted frames, draw_calls/frame="
                      << static_cast<double>(drawCalls) / kBenchmarkFramesPerRun
                      << ", gpu_ms/frame="
                      << (gpuTimestampFrameCount_ == 0 || gpuTimestampFrequency_ == 0
                              ? 0.0
                              : static_cast<double>(gpuTimestampTicksTotal_) * 1000.0 /
                                    static_cast<double>(gpuTimestampFrequency_) /
                                    static_cast<double>(gpuTimestampFrameCount_))
                      << " (" << gpuTimestampFrameCount_ << " timestamped frames)\n";
        }

        std::vector<double> sortedRuns = runMilliseconds;
        std::sort(sortedRuns.begin(), sortedRuns.end());
        const double medianMilliseconds =
            sortedRuns[sortedRuns.size() / 2];
        std::cout << "Median: " << std::fixed << std::setprecision(3)
                  << medianMilliseconds << " ms per "
                  << kBenchmarkFramesPerRun << "-frame run, "
                  << medianMilliseconds / kBenchmarkFramesPerRun
                  << " ms/frame\n"
                  << "Submitted draw frames: " << submittedFrames << '\n';
        CheckDebugMessages();
    }

    void RunRendererBenchmark()
    {
        RunRendererBenchmark(5000, false);
    }

    void RunGpuBenchmark()
    {
        RunRendererBenchmark(10000, true);
    }

    void RunCircleThroughputBenchmark()
    {
        if (!circleThroughputBenchmarkEnabled_ ||
            circleThroughputTargetCount_ == 0 ||
            circleThroughputShapes_.size() != circleThroughputTargetCount_ ||
            !drawingShapes_.empty())
        {
            throw std::runtime_error(
                "Circle-throughput benchmark was not initialized with an empty canvas and complete scene");
        }
        if (!QueryPerformanceFrequency(&circleThroughputQpcFrequency_) ||
            circleThroughputQpcFrequency_.QuadPart <= 0)
        {
            throw std::runtime_error(
                "QueryPerformanceFrequency failed for circle-throughput benchmark");
        }

        circleThroughputMetrics_ = {};
        circleThroughputMetrics_.batchSize = circleThroughputBatchSize_;
        circleThroughputMetrics_.sceneDataPreparationMilliseconds =
            static_cast<double>(circleThroughputScenePreparationTicks_) *
            1000.0 /
            static_cast<double>(circleThroughputQpcFrequency_.QuadPart);
        gpuTimestampTicksTotal_ = 0;
        gpuTimestampFrameCount_ = 0;

        LARGE_INTEGER measurementStart{};
        FILETIME cpuStartCreation{}, cpuStartExit{}, cpuStartKernel{}, cpuStartUser{};
        ULONGLONG allCyclesStart = 0;
        if (!QueryPerformanceCounter(&measurementStart) ||
            !GetThreadTimes(
                GetCurrentThread(), &cpuStartCreation, &cpuStartExit,
                &cpuStartKernel, &cpuStartUser) ||
            !QueryThreadCycleTime(GetCurrentThread(), &allCyclesStart))
        {
            throw std::runtime_error(
                "Could not start circle-throughput measurement counters");
        }
        circleThroughputBenchmarkActive_ = true;
        std::size_t nextShape = 0;
        while (nextShape < circleThroughputShapes_.size())
        {
            const UINT batchCount = static_cast<UINT>(std::min<std::size_t>(
                circleThroughputBatchSize_,
                circleThroughputShapes_.size() - nextShape));
            const std::size_t firstShapeIndex = drawingShapes_.size();
            LARGE_INTEGER additionStart{};
            LARGE_INTEGER additionStop{};
            if (!QueryPerformanceCounter(&additionStart))
            {
                throw std::runtime_error(
                    "QueryPerformanceCounter failed before scene insertion");
            }
            drawingShapes_.insert(
                drawingShapes_.end(),
                circleThroughputShapes_.begin() +
                    static_cast<std::ptrdiff_t>(nextShape),
                circleThroughputShapes_.begin() +
                    static_cast<std::ptrdiff_t>(nextShape + batchCount));
            if (!QueryPerformanceCounter(&additionStop))
            {
                throw std::runtime_error(
                    "QueryPerformanceCounter failed after scene insertion");
            }
            AddCircleThroughputElapsed(
                circleThroughputMetrics_.sceneAdditionElapsedMilliseconds,
                static_cast<UINT64>(
                    additionStop.QuadPart - additionStart.QuadPart));
            OnInteractiveShapesAdded(firstShapeIndex, batchCount);
            circleThroughputMetrics_.circlesAddedToScene += batchCount;
            Render();
            nextShape += batchCount;
        }

        if (rendererMode_ != RendererMode::Direct2D)
        {
            LARGE_INTEGER drainStart{};
            LARGE_INTEGER drainStop{};
            if (!QueryPerformanceCounter(&drainStart))
            {
                throw std::runtime_error(
                    "QueryPerformanceCounter failed before final D3D12 fence drain");
            }
            WaitForGpu();
            if (!QueryPerformanceCounter(&drainStop))
            {
                throw std::runtime_error(
                    "QueryPerformanceCounter failed after final D3D12 fence drain");
            }
            AddCircleThroughputElapsed(
                circleThroughputMetrics_.d3dFinalFenceDrainMilliseconds,
                static_cast<UINT64>(
                    drainStop.QuadPart - drainStart.QuadPart));
            if (canvasRenderedShapeCount_ != circleThroughputTargetCount_ ||
                circleThroughputMetrics_.circlesDrawnIntoCanvas !=
                    circleThroughputTargetCount_)
            {
                circleThroughputBenchmarkActive_ = false;
                throw std::runtime_error(
                    "D3D12 fence completed without drawing every throughput circle into the persistent canvas");
            }
            circleThroughputMetrics_.gpuDrawTimeAvailable =
                gpuTimestampEnabled_ && gpuTimestampFrequency_ != 0 &&
                gpuTimestampFrameCount_ ==
                    circleThroughputMetrics_.canvasUpdates;
            if (circleThroughputMetrics_.gpuDrawTimeAvailable)
            {
                circleThroughputMetrics_.d3dGpuDrawMilliseconds =
                    static_cast<double>(gpuTimestampTicksTotal_) * 1000.0 /
                    static_cast<double>(gpuTimestampFrequency_);
            }
        }
        else
        {
            if (circleThroughputMetrics_.circlesDrawnIntoCanvas <
                circleThroughputTargetCount_)
            {
                circleThroughputBenchmarkActive_ = false;
                throw std::runtime_error(
                    "Direct2D did not successfully draw every throughput circle into the persistent canvas");
            }
            circleThroughputMetrics_.circlesDrawnIntoCanvas =
                circleThroughputTargetCount_;
        }
        FILETIME cpuStopCreation{}, cpuStopExit{}, cpuStopKernel{}, cpuStopUser{};
        ULONGLONG allCyclesStop = 0;
        if (!GetThreadTimes(
                GetCurrentThread(), &cpuStopCreation, &cpuStopExit,
                &cpuStopKernel, &cpuStopUser) ||
            !QueryThreadCycleTime(GetCurrentThread(), &allCyclesStop))
        {
            circleThroughputBenchmarkActive_ = false;
            throw std::runtime_error(
                "Could not stop circle-throughput CPU counters");
        }
        circleThroughputBenchmarkActive_ = false;

        LARGE_INTEGER measurementStop{};
        if (!QueryPerformanceCounter(&measurementStop) ||
            measurementStop.QuadPart <= measurementStart.QuadPart)
        {
            throw std::runtime_error(
                "Could not determine circle-throughput measurement duration");
        }
        circleThroughputMetrics_.measurementElapsedMilliseconds =
            static_cast<double>(
                measurementStop.QuadPart - measurementStart.QuadPart) *
            1000.0 /
            static_cast<double>(circleThroughputQpcFrequency_.QuadPart);
        const UINT64 threadCpu100ns =
            FileTimeValue(cpuStopKernel) + FileTimeValue(cpuStopUser) -
            FileTimeValue(cpuStartKernel) - FileTimeValue(cpuStartUser);
        const ULONGLONG threadCycles = allCyclesStop - allCyclesStart;
        const double cpuMillisecondsPerCycle = threadCycles == 0
            ? 0.0
            : (static_cast<double>(threadCpu100ns) / 10000.0) /
                static_cast<double>(threadCycles);
        circleThroughputMetrics_.canvasDrawCommandIssueCpuMilliseconds =
            static_cast<double>(
                circleThroughputMetrics_.canvasDrawCommandIssueCycles) *
            cpuMillisecondsPerCycle;
        circleThroughputMetrics_.canvasUpdateEndDrawCpuMilliseconds =
            static_cast<double>(
                circleThroughputMetrics_.canvasUpdateEndDrawCycles) *
            cpuMillisecondsPerCycle;
        if (circleThroughputMetrics_.measurementElapsedMilliseconds <= 0.0 ||
            circleThroughputMetrics_.displayFrames == 0)
        {
            throw std::runtime_error(
                "Circle-throughput benchmark completed without a measurable displayed frame");
        }
        circleThroughputMetrics_.circlesAddedPerSecond =
            static_cast<double>(
                circleThroughputMetrics_.circlesAddedToScene) * 1000.0 /
            circleThroughputMetrics_.measurementElapsedMilliseconds;
        circleThroughputMetrics_.circlesDrawnIntoCanvasPerSecond =
            static_cast<double>(
                circleThroughputMetrics_.circlesDrawnIntoCanvas) * 1000.0 /
            circleThroughputMetrics_.measurementElapsedMilliseconds;
        circleThroughputMetrics_.canvasCompletionElapsedMilliseconds =
            circleThroughputMetrics_.measurementElapsedMilliseconds;
        circleThroughputMetrics_.endToEndElapsedMilliseconds =
            circleThroughputMetrics_.measurementElapsedMilliseconds +
            circleThroughputMetrics_.sceneDataPreparationMilliseconds;
        circleThroughputMetrics_.circlesAddedEndToEndPerSecond =
            static_cast<double>(
                circleThroughputMetrics_.circlesAddedToScene) * 1000.0 /
            circleThroughputMetrics_.endToEndElapsedMilliseconds;
        circleThroughputMetrics_.circlesDrawnEndToEndPerSecond =
            static_cast<double>(
                circleThroughputMetrics_.circlesDrawnIntoCanvas) * 1000.0 /
            circleThroughputMetrics_.endToEndElapsedMilliseconds;

        std::cout << std::fixed << std::setprecision(3)
                  << "[circle-throughput] status=success renderer="
                  << RendererModeName(rendererMode_)
                  << " seed=" << kSeed << " scene_hash=0x"
                  << std::hex << std::uppercase
                  << circleThroughputSceneHash_ << std::dec << std::nouppercase
                  << " circles_added=" << circleThroughputMetrics_.circlesAddedToScene
                  << " circles_drawn_into_canvas="
                  << circleThroughputMetrics_.circlesDrawnIntoCanvas
                  << " batch_size=" << circleThroughputMetrics_.batchSize
                  << " displayed_frames=" << circleThroughputMetrics_.displayFrames
                  << " canvas_updates=" << circleThroughputMetrics_.canvasUpdates
                  << " elapsed_ms="
                  << circleThroughputMetrics_.measurementElapsedMilliseconds
                  << " added_per_second="
                  << circleThroughputMetrics_.circlesAddedPerSecond
                  << " drawn_into_canvas_per_second="
                  << circleThroughputMetrics_.circlesDrawnIntoCanvasPerSecond
                  << " end_to_end_added_per_second="
                  << circleThroughputMetrics_.circlesAddedEndToEndPerSecond
                  << " gpu_draw_ms=";
        if (circleThroughputMetrics_.gpuDrawTimeAvailable)
        {
            std::cout << circleThroughputMetrics_.d3dGpuDrawMilliseconds;
        }
        else
        {
            std::cout << "unavailable";
        }
        std::cout << '\n';
        StoreCircleThroughputBenchmarkJson();
    }

    void RunRendererBenchmark(UINT measurementMilliseconds, bool gpuBenchmark)
    {
        if (rendererMode_ == RendererMode::Direct2D)
        {
            if (gpuBenchmark)
            {
                throw std::runtime_error(
                    "Direct2D does not use the D3D12 GPU timestamp-query benchmark.");
            }
            RunDirect2DBenchmark(measurementMilliseconds);
            return;
        }

        LARGE_INTEGER frequency{};
        if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0)
        {
            throw std::runtime_error("QueryPerformanceFrequency failed for renderer benchmark");
        }
        if (gpuBenchmark && (!gpuTimestampEnabled_ || gpuTimestampFrequency_ == 0))
        {
            throw std::runtime_error(
                "GPU benchmark requires available D3D12 timestamp queries and a nonzero timestamp frequency");
        }
        if (circleParameters_.size() != requestedCircleCount_ ||
            circleDraws_.size() != requestedCircleCount_ ||
            (rendererMode_ == RendererMode::Shader &&
             shaderCircleInstances_.size() != requestedCircleCount_))
        {
            throw std::runtime_error(
                "Renderer benchmark scene counts do not match the requested count");
        }
        const std::uint64_t sceneHash = HashCircleScene(circleParameters_);
        std::cout << (gpuBenchmark
                          ? "D3D12 GPU rendering benchmark\n"
                          : "Presented renderer benchmark\n")
                  << "Renderer: " << RendererModeName(rendererMode_) << '\n'
                  << "Circles: " << circleDraws_.size() << '\n'
                  << "Client size: " << width_ << 'x' << height_ << '\n'
                  << "Warm-up: 3000 ms; measured: "
                  << measurementMilliseconds << " ms\n"
                  << "Scene seed: " << kSeed << "; scene hash: 0x"
                  << std::hex << std::uppercase << sceneHash << std::dec
                  << std::nouppercase << '\n';
        if (gpuBenchmark)
        {
            std::wcout << L"GPU adapter: "
                       << selectedAdapterDescription_.Description
                       << L" (vendor_id=0x" << std::hex << std::uppercase
                       << selectedAdapterDescription_.VendorId << std::dec
                       << L", dedicated_video_memory_bytes="
                       << selectedAdapterDescription_.DedicatedVideoMemory
                       << L")\n" << std::flush;
            std::cout
                << "The scene and GPU buffers are prepared before warm-up; no circles or geometry are generated during measurement.\n"
                << "GPU timestamps bracket draw commands, excluding command preparation and Present.\n"
                << (rendererMode_ == RendererMode::Legacy
                        ? "Legacy uses one draw call per circle; its CPU command-recording overhead is reported separately.\n"
                        : "Shader uses one instanced draw for the prepared circle instances.\n");
        }
        else
        {
            std::cout
                << "Swap-chain Present uses the existing unchanged parameters.\n"
                << "GPU timestamps cover draw work only; per-frame CPU samples split command recording, ExecuteCommandLists, and Present.\n";
        }

        auto nowTicks = [&frequency]() {
            LARGE_INTEGER value{};
            if (!QueryPerformanceCounter(&value))
            {
                throw std::runtime_error("QueryPerformanceCounter failed in renderer benchmark");
            }
            return value.QuadPart;
        };
        const LONGLONG warmupStart = nowTicks();
        const LONGLONG warmupEnd = warmupStart +
            static_cast<LONGLONG>(frequency.QuadPart * 3);
        while (nowTicks() < warmupEnd)
        {
            Render();
        }
        WaitForGpu();
        gpuTimestampTicksTotal_ = 0;
        gpuTimestampFrameCount_ = 0;
        rendererBenchmarkFrameSamples_.clear();
        rendererBenchmarkFrameSamples_.reserve(65536);
        rendererBenchmarkGpuTicks_.clear();
        rendererBenchmarkGpuTicks_.reserve(65536);
        rendererBenchmarkGpuQueriesSubmitted_ = 0;
        rendererBenchmarkGpuInvalidSamples_ = 0;
        rendererBenchmarkGpuSampling_ = true;
        rendererBenchmarkRecordCycles_ = 0;
        rendererBenchmarkExecuteCycles_ = 0;
        rendererBenchmarkRecordElapsedTicks_ = 0;
        rendererBenchmarkExecuteElapsedTicks_ = 0;

        FILETIME cpuStartCreation{}, cpuStartExit{}, cpuStartKernel{}, cpuStartUser{};
        FILETIME cpuStopCreation{}, cpuStopExit{}, cpuStopKernel{}, cpuStopUser{};
        ULONGLONG allCyclesStart = 0;
        ULONGLONG allCyclesStop = 0;
        if (!QueryPerformanceCounter(&rendererBenchmarkMeasurementStart_))
        {
            throw std::runtime_error("QueryPerformanceCounter failed at render measurement start");
        }
        rendererBenchmarkMeasurementEnd_.QuadPart =
            rendererBenchmarkMeasurementStart_.QuadPart +
            frequency.QuadPart * measurementMilliseconds / 1000;
        if (!GetThreadTimes(
                GetCurrentThread(), &cpuStartCreation, &cpuStartExit,
                &cpuStartKernel, &cpuStartUser) ||
            !QueryThreadCycleTime(GetCurrentThread(), &allCyclesStart))
        {
            throw std::runtime_error("Could not read thread CPU counters at render measurement start");
        }
        rendererBenchmarkPresentReturns_ = 0;
        rendererBenchmarkDrawCalls_ = 0;
        rendererBenchmarkRenderedCircles_ = 0;
        rendererBenchmarkActive_ = true;
        while (nowTicks() < rendererBenchmarkMeasurementEnd_.QuadPart)
        {
            Render();
        }
        rendererBenchmarkActive_ = false;
        if (!GetThreadTimes(
                GetCurrentThread(), &cpuStopCreation, &cpuStopExit,
                &cpuStopKernel, &cpuStopUser) ||
            !QueryThreadCycleTime(GetCurrentThread(), &allCyclesStop))
        {
            throw std::runtime_error("Could not read thread CPU counters at render measurement end");
        }
        WaitForGpu();
        rendererBenchmarkGpuSampling_ = false;

        if (gpuBenchmark &&
            (rendererBenchmarkGpuTicks_.empty() ||
             rendererBenchmarkGpuInvalidSamples_ != 0 ||
             rendererBenchmarkGpuTicks_.size() !=
                 rendererBenchmarkGpuQueriesSubmitted_))
        {
            std::ostringstream message;
            message << "GPU benchmark timestamp validation failed: "
                    << rendererBenchmarkGpuQueriesSubmitted_
                    << " query pairs submitted, "
                    << rendererBenchmarkGpuTicks_.size()
                    << " valid results, "
                    << rendererBenchmarkGpuInvalidSamples_
                    << " invalid results, timestamp frequency "
                    << gpuTimestampFrequency_ << " Hz";
            throw std::runtime_error(message.str());
        }
        if (rendererBenchmarkFrameSamples_.empty() ||
            rendererBenchmarkPresentReturns_ != rendererBenchmarkFrameSamples_.size())
        {
            throw std::runtime_error(
                "Renderer benchmark did not collect a consistent set of presented frames");
        }

        const ULONGLONG threadCpu100ns =
            FileTimeValue(cpuStopKernel) + FileTimeValue(cpuStopUser) -
            FileTimeValue(cpuStartKernel) - FileTimeValue(cpuStartUser);
        const ULONGLONG threadCycles = allCyclesStop - allCyclesStart;
        const double cpuMsPerCycle = threadCycles == 0
            ? 0.0
            : (static_cast<double>(threadCpu100ns) / 10000.0) /
                static_cast<double>(threadCycles);
        const double seconds =
            static_cast<double>(measurementMilliseconds) / 1000.0;
        const auto frameMetric = [this, cpuMsPerCycle, &frequency](auto selector,
                                                                  bool cpuCycles) {
            std::vector<double> values;
            values.reserve(rendererBenchmarkFrameSamples_.size());
            for (const RendererFrameSample& frame : rendererBenchmarkFrameSamples_)
            {
                const double raw = static_cast<double>(selector(frame));
                values.push_back(cpuCycles
                    ? raw * cpuMsPerCycle
                    : raw * 1000.0 / static_cast<double>(frequency.QuadPart));
            }
            return SummarizeSamples(std::move(values));
        };
        const SampleSummary recordCpu = frameMetric(
            [](const RendererFrameSample& frame) { return frame.recordCycles; }, true);
        const SampleSummary executeCpu = frameMetric(
            [](const RendererFrameSample& frame) { return frame.executeCycles; }, true);
        const SampleSummary presentCpu = frameMetric(
            [](const RendererFrameSample& frame) { return frame.presentCycles; }, true);
        const SampleSummary recordElapsed = frameMetric(
            [](const RendererFrameSample& frame) { return frame.recordElapsedTicks; }, false);
        const SampleSummary executeElapsed = frameMetric(
            [](const RendererFrameSample& frame) { return frame.executeElapsedTicks; }, false);
        const SampleSummary presentElapsed = frameMetric(
            [](const RendererFrameSample& frame) { return frame.presentElapsedTicks; }, false);
        std::vector<double> gpuMilliseconds;
        gpuMilliseconds.reserve(rendererBenchmarkGpuTicks_.size());
        for (const UINT64 ticks : rendererBenchmarkGpuTicks_)
        {
            gpuMilliseconds.push_back(
                static_cast<double>(ticks) * 1000.0 /
                static_cast<double>(gpuTimestampFrequency_));
        }
        const SampleSummary gpuDraw = SummarizeSamples(std::move(gpuMilliseconds));
        const double frameCount = static_cast<double>(rendererBenchmarkFrameSamples_.size());
        const SampleSummary drawCalls = SummarizeSamples([&]() {
            std::vector<double> values;
            values.reserve(rendererBenchmarkFrameSamples_.size());
            for (const RendererFrameSample& frame : rendererBenchmarkFrameSamples_)
            {
                values.push_back(static_cast<double>(frame.drawCalls));
            }
            return values;
        }());
        const SampleSummary renderedCircles = SummarizeSamples([&]() {
            std::vector<double> values;
            values.reserve(rendererBenchmarkFrameSamples_.size());
            for (const RendererFrameSample& frame : rendererBenchmarkFrameSamples_)
            {
                values.push_back(static_cast<double>(frame.renderedCircles));
            }
            return values;
        }());
        if (gpuBenchmark)
        {
            const double completedFrames =
                static_cast<double>(rendererBenchmarkGpuTicks_.size());
            std::cout << std::fixed << std::setprecision(3)
                      << "[gpu-benchmark] result renderer="
                      << RendererModeName(rendererMode_)
                      << " gpu_vendor_id=0x" << std::hex << std::uppercase
                      << selectedAdapterDescription_.VendorId << std::dec
                      << std::nouppercase
                      << " dedicated_video_memory_bytes="
                      << selectedAdapterDescription_.DedicatedVideoMemory
                      << " size=" << width_ << 'x' << height_
                      << " circles=" << circleParameters_.size()
                      << " instances=" << shaderCircleInstances_.size()
                      << " seed=" << kSeed
                      << " scene_hash=0x" << std::hex << std::uppercase
                      << sceneHash << std::dec << std::nouppercase
                      << " measured_ms=" << measurementMilliseconds
                      << " timestamp_frequency_hz=" << gpuTimestampFrequency_
                      << " timestamp_query_pairs_submitted="
                      << rendererBenchmarkGpuQueriesSubmitted_
                      << " timestamp_query_pairs_valid="
                      << rendererBenchmarkGpuTicks_.size()
                      << " invalid_timestamp_results="
                      << rendererBenchmarkGpuInvalidSamples_
                      << " timestamp_validation=passed"
                      << " gpu_completed_frames=" << completedFrames
                      << " gpu_completed_frames_per_s="
                      << completedFrames / seconds
                      << " present_returns="
                      << rendererBenchmarkPresentReturns_
                      << " present_returns_per_s=" << frameCount / seconds
                      << " gpu_draw_mean_ms=" << gpuDraw.mean
                      << " gpu_draw_median_ms=" << gpuDraw.median
                      << " gpu_draw_p95_ms=" << gpuDraw.p95
                      << " command_record_cpu_ms_per_frame_est="
                      << recordCpu.mean
                      << " command_execute_cpu_ms_per_frame_est="
                      << executeCpu.mean
                      << " present_cpu_ms_per_frame_est=" << presentCpu.mean
                      << " present_elapsed_ms_per_frame="
                      << presentElapsed.mean
                      << " draw_calls_per_frame=" << drawCalls.mean
                      << '\n' << std::flush;
        }
        else
        {
            std::cout << std::fixed << std::setprecision(3)
                  << "[renderer-benchmark] result renderer=" << RendererModeName(rendererMode_)
                  << " size=" << width_ << 'x' << height_
                  << " circles=" << circleParameters_.size()
                  << " instances=" << shaderCircleInstances_.size()
                  << " seed=" << kSeed
                  << " scene_hash=0x" << std::hex << std::uppercase << sceneHash
                  << std::dec << std::nouppercase
                  << " measured_ms=5000"
                  << " presented_frames=" << rendererBenchmarkFrameSamples_.size()
                  << " present_returns=" << rendererBenchmarkPresentReturns_
                  << " present_returns_per_s=" << frameCount / seconds
                  << " draw_calls_per_frame=" << drawCalls.mean
                  << " rendered_circles_per_frame=" << renderedCircles.mean
                  << " rendered_circles_min=" << renderedCircles.minimum
                  << " rendered_circles_max=" << renderedCircles.maximum
                  << " command_record_cpu_ms_per_frame_est="
                  << recordCpu.mean
                  << " command_execute_cpu_ms_per_frame_est="
                  << executeCpu.mean
                  << " present_cpu_ms_per_frame_est=" << presentCpu.mean
                  << " command_record_elapsed_ms_per_frame="
                  << recordElapsed.mean
                  << " command_execute_elapsed_ms_per_frame="
                  << executeElapsed.mean
                  << " present_elapsed_ms_per_frame=" << presentElapsed.mean
                  << " ui_thread_cpu_ms_per_s="
                  << (static_cast<double>(threadCpu100ns) / 10000.0) / seconds
                  << " gpu_draw_mean_ms=" << gpuDraw.mean
                  << " gpu_draw_median_ms=" << gpuDraw.median
                  << " gpu_draw_p95_ms=" << gpuDraw.p95
                  << " gpu_draw_min_ms=" << gpuDraw.minimum
                  << " gpu_draw_max_ms=" << gpuDraw.maximum
                  << " gpu_timestamp_frames=" << rendererBenchmarkGpuTicks_.size()
                  << " record_cpu_median_ms_est=" << recordCpu.median
                  << " record_cpu_p95_ms_est=" << recordCpu.p95
                  << " record_cpu_min_ms_est=" << recordCpu.minimum
                  << " record_cpu_max_ms_est=" << recordCpu.maximum
                  << " execute_cpu_median_ms_est=" << executeCpu.median
                  << " execute_cpu_p95_ms_est=" << executeCpu.p95
                  << " execute_cpu_min_ms_est=" << executeCpu.minimum
                  << " execute_cpu_max_ms_est=" << executeCpu.maximum
                  << " present_cpu_median_ms_est=" << presentCpu.median
                  << " present_cpu_p95_ms_est=" << presentCpu.p95
                  << " present_cpu_min_ms_est=" << presentCpu.minimum
                  << " present_cpu_max_ms_est=" << presentCpu.maximum
                  << " record_elapsed_median_ms=" << recordElapsed.median
                  << " record_elapsed_p95_ms=" << recordElapsed.p95
                  << " record_elapsed_min_ms=" << recordElapsed.minimum
                  << " record_elapsed_max_ms=" << recordElapsed.maximum
                  << " execute_elapsed_median_ms=" << executeElapsed.median
                  << " execute_elapsed_p95_ms=" << executeElapsed.p95
                  << " execute_elapsed_min_ms=" << executeElapsed.minimum
                  << " execute_elapsed_max_ms=" << executeElapsed.maximum
                  << " present_elapsed_median_ms=" << presentElapsed.median
                  << " present_elapsed_p95_ms=" << presentElapsed.p95
                  << " present_elapsed_min_ms=" << presentElapsed.minimum
                  << " present_elapsed_max_ms=" << presentElapsed.maximum
                  << "\n" << std::flush;
        }
        CheckDebugMessages();
        if (!gpuBenchmark)
        {
            StoreRendererBenchmarkJson(
                measurementMilliseconds,
                sceneHash,
                static_cast<UINT64>(rendererBenchmarkFrameSamples_.size()),
                drawCalls.mean,
                !rendererBenchmarkGpuTicks_.empty(),
                gpuDraw.mean,
                false,
                0.0,
                false,
                0,
                0,
                0.0,
                {
                    {"command_record_cpu_est_ms_per_frame", recordCpu.mean},
                    {"command_execute_cpu_est_ms_per_frame", executeCpu.mean},
                    {"present_cpu_est_ms_per_frame", presentCpu.mean},
                    {"command_record_elapsed_ms_per_frame", recordElapsed.mean},
                    {"command_execute_elapsed_ms_per_frame", executeElapsed.mean},
                    {"present_elapsed_ms_per_frame", presentElapsed.mean},
                });
        }
    }

    void RunDirect2DBenchmark(UINT measurementMilliseconds)
    {
        LARGE_INTEGER frequency{};
        if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0)
        {
            throw std::runtime_error(
                "QueryPerformanceFrequency failed for Direct2D renderer benchmark");
        }
        if (circleParameters_.size() != requestedCircleCount_)
        {
            throw std::runtime_error(
                "Direct2D benchmark scene count does not match the requested count");
        }

        const std::uint64_t sceneHash = HashCircleScene(circleParameters_);
        std::cout << "Presented Direct2D renderer benchmark\n"
                  << "Renderer: " << RendererModeName(rendererMode_) << '\n'
                  << "Target: ID2D1HwndRenderTarget; requested_target_type=default"
                  << "; hardware_acceleration=unknown\n"
                  << "GPU adapter: unknown (ID2D1HwndRenderTarget adapter is not queryable)\n"
                  << "GPU selection: not controlled by Direct2D HWND render targets; requested --gpu "
                  << GpuSelectionLabel(gpuSelection_) << '\n'
                  << "Circles: " << circleParameters_.size() << '\n'
                  << "Client size: " << width_ << 'x' << height_ << '\n'
                  << "Scene seed: " << kSeed << "; scene hash: 0x"
                  << std::hex << std::uppercase << sceneHash << std::dec
                  << std::nouppercase << '\n'
                  << "Scene data preparation: "
                  << static_cast<double>(direct2DScenePreparationTicks_) *
                         1000.0 / static_cast<double>(frequency.QuadPart)
                  << " ms before warm-up; excluded from frame timing.\n"
                  << "New circles update the persistent bitmap once; measured frames only blit that bitmap.\n"
                  << "Canvas-update, bitmap-blit, and EndDraw times are CPU/API elapsed times; GPU execution time is unavailable.\n"
                  << "EndDraw is not treated as GPU completion, and HWND target presentation is not directly comparable to D3D12 Present settings.\n";

        const auto nowTicks = [&frequency]() {
            LARGE_INTEGER value{};
            if (!QueryPerformanceCounter(&value))
            {
                throw std::runtime_error(
                    "QueryPerformanceCounter failed in Direct2D renderer benchmark");
            }
            return value.QuadPart;
        };
        const LONGLONG warmupEnd =
            nowTicks() + static_cast<LONGLONG>(frequency.QuadPart * 3);
        while (nowTicks() < warmupEnd)
        {
            Render();
        }

        direct2DBenchmarkSamples_.clear();
        direct2DBenchmarkSamples_.reserve(65536);
        rendererBenchmarkMeasurementEnd_.QuadPart =
            nowTicks() + frequency.QuadPart * measurementMilliseconds / 1000;
        FILETIME cpuStartCreation{}, cpuStartExit{}, cpuStartKernel{}, cpuStartUser{};
        FILETIME cpuStopCreation{}, cpuStopExit{}, cpuStopKernel{}, cpuStopUser{};
        ULONGLONG allCyclesStart = 0;
        ULONGLONG allCyclesStop = 0;
        if (!QueryPerformanceCounter(&rendererBenchmarkMeasurementStart_) ||
            !GetThreadTimes(
                GetCurrentThread(), &cpuStartCreation, &cpuStartExit,
                &cpuStartKernel, &cpuStartUser) ||
            !QueryThreadCycleTime(GetCurrentThread(), &allCyclesStart))
        {
            throw std::runtime_error(
                "Could not start Direct2D renderer benchmark counters");
        }
        direct2DBenchmarkActive_ = true;
        while (nowTicks() < rendererBenchmarkMeasurementEnd_.QuadPart)
        {
            Render();
        }
        direct2DBenchmarkActive_ = false;
        if (!GetThreadTimes(
                GetCurrentThread(), &cpuStopCreation, &cpuStopExit,
                &cpuStopKernel, &cpuStopUser) ||
            !QueryThreadCycleTime(GetCurrentThread(), &allCyclesStop))
        {
            throw std::runtime_error(
                "Could not stop Direct2D renderer benchmark counters");
        }
        if (direct2DBenchmarkSamples_.empty())
        {
            throw std::runtime_error(
                "Direct2D renderer benchmark collected no completed EndDraw frames");
        }
        if (direct2DNewlyRenderedCircleCount_ != requestedCircleCount_)
        {
            throw std::runtime_error(
                "Direct2D did not add the complete benchmark scene to its persistent canvas");
        }
        for (const Direct2DFrameSample& sample : direct2DBenchmarkSamples_)
        {
            if (sample.drawCalls != 1)
            {
                throw std::runtime_error(
                    "Direct2D renderer benchmark did not submit one bitmap blit per frame");
            }
        }

        const ULONGLONG threadCpu100ns =
            FileTimeValue(cpuStopKernel) + FileTimeValue(cpuStopUser) -
            FileTimeValue(cpuStartKernel) - FileTimeValue(cpuStartUser);
        const ULONGLONG threadCycles = allCyclesStop - allCyclesStart;
        const double cpuMsPerCycle = threadCycles == 0
            ? 0.0
            : (static_cast<double>(threadCpu100ns) / 10000.0) /
                  static_cast<double>(threadCycles);
        const auto summarize = [this, cpuMsPerCycle, &frequency](
                                   auto selector, bool cpuCycles) {
            std::vector<double> values;
            values.reserve(direct2DBenchmarkSamples_.size());
            for (const Direct2DFrameSample& sample : direct2DBenchmarkSamples_)
            {
                const double value = static_cast<double>(selector(sample));
                values.push_back(cpuCycles
                    ? value * cpuMsPerCycle
                    : value * 1000.0 / static_cast<double>(frequency.QuadPart));
            }
            return SummarizeSamples(std::move(values));
        };
        const SampleSummary drawIssueCpu = summarize(
            [](const Direct2DFrameSample& sample) {
                return sample.drawIssueCycles;
            },
            true);
        const SampleSummary endDrawCpu = summarize(
            [](const Direct2DFrameSample& sample) {
                return sample.endDrawCycles;
            },
            true);
        const SampleSummary drawIssueElapsed = summarize(
            [](const Direct2DFrameSample& sample) {
                return sample.drawIssueElapsedTicks;
            },
            false);
        const SampleSummary endDrawElapsed = summarize(
            [](const Direct2DFrameSample& sample) {
                return sample.endDrawElapsedTicks;
            },
            false);
        const SampleSummary frameElapsed = summarize(
            [](const Direct2DFrameSample& sample) {
                return sample.frameElapsedTicks;
            },
            false);
        std::vector<double> drawCallValues;
        drawCallValues.reserve(direct2DBenchmarkSamples_.size());
        for (const Direct2DFrameSample& sample : direct2DBenchmarkSamples_)
        {
            drawCallValues.push_back(static_cast<double>(sample.drawCalls));
        }
        const SampleSummary drawCalls =
            SummarizeSamples(std::move(drawCallValues));
        const double measuredSeconds =
            static_cast<double>(measurementMilliseconds) / 1000.0;
        std::cout << std::fixed << std::setprecision(3)
                  << "[renderer-benchmark] result renderer=direct2d"
                  << " target_type=ID2D1HwndRenderTarget"
                  << " requested_target_type=default"
                  << " hardware_acceleration=unknown"
                  << " gpu_adapter=unknown gpu_time=unavailable"
                  << " size=" << width_ << 'x' << height_
                  << " circles=" << circleParameters_.size()
                  << " seed=" << kSeed
                  << " scene_hash=0x" << std::hex << std::uppercase << sceneHash
                  << std::dec << std::nouppercase
                  << " measured_ms=" << measurementMilliseconds
                  << " scene_data_preparation_ms="
                  << static_cast<double>(direct2DScenePreparationTicks_) *
                         1000.0 / static_cast<double>(frequency.QuadPart)
                  << " completed_enddraw_frames=" << direct2DBenchmarkSamples_.size()
                  << " frames_per_s="
                  << static_cast<double>(direct2DBenchmarkSamples_.size()) /
                         measuredSeconds
                  << " newly_rendered_circles="
                  << direct2DNewlyRenderedCircleCount_
                  << " canvas_update_count=" << direct2DCanvasUpdateCount_
                  << " canvas_update_elapsed_ms="
                  << static_cast<double>(direct2DCanvasUpdateTicks_) *
                         1000.0 / static_cast<double>(frequency.QuadPart)
                  << " canvas_update_elapsed_ms_per_update="
                  << (direct2DCanvasUpdateCount_ == 0
                          ? 0.0
                          : static_cast<double>(direct2DCanvasUpdateTicks_) *
                                1000.0 /
                                static_cast<double>(frequency.QuadPart) /
                                static_cast<double>(direct2DCanvasUpdateCount_))
                  << " bitmap_blits_per_frame=" << drawCalls.mean
                  << " bitmap_blit_cpu_ms_per_frame_est=" << drawIssueCpu.mean
                  << " bitmap_blit_elapsed_ms_per_frame=" << drawIssueElapsed.mean
                  << " enddraw_cpu_ms_per_frame_est=" << endDrawCpu.mean
                  << " enddraw_elapsed_ms_per_frame=" << endDrawElapsed.mean
                  << " render_api_cpu_ms_per_frame_est="
                  << drawIssueCpu.mean + endDrawCpu.mean
                  << " frame_elapsed_ms_per_frame=" << frameElapsed.mean
                  << " frame_elapsed_p95_ms=" << frameElapsed.p95
                  << " ui_thread_cpu_ms_per_s="
                  << (static_cast<double>(threadCpu100ns) / 10000.0) /
                         measuredSeconds
                  << " application_wait_message_calls="
                  << direct2DDiagnosticApplicationWaitCount_
                  << " application_wait_message_elapsed_ms="
                  << static_cast<double>(
                         direct2DDiagnosticApplicationWaitTicks_) *
                         1000.0 /
                         static_cast<double>(frequency.QuadPart)
                  << " gpu_execution_time=not_measured\n" << std::flush;
        StoreRendererBenchmarkJson(
            measurementMilliseconds,
            sceneHash,
            static_cast<UINT64>(direct2DBenchmarkSamples_.size()),
            drawCalls.mean,
            false,
            0.0,
            true,
            static_cast<double>(direct2DScenePreparationTicks_) *
                1000.0 / static_cast<double>(frequency.QuadPart),
            true,
            direct2DNewlyRenderedCircleCount_,
            direct2DCanvasUpdateCount_,
            static_cast<double>(direct2DCanvasUpdateTicks_) *
                1000.0 / static_cast<double>(frequency.QuadPart),
            {
                {"bitmap_blit_cpu_est_ms_per_frame", drawIssueCpu.mean},
                {"bitmap_blit_elapsed_ms_per_frame", drawIssueElapsed.mean},
                {"enddraw_cpu_est_ms_per_frame", endDrawCpu.mean},
                {"enddraw_elapsed_ms_per_frame", endDrawElapsed.mean},
                {"render_api_cpu_est_ms_per_frame",
                 drawIssueCpu.mean + endDrawCpu.mean},
                {"frame_elapsed_ms_per_frame", frameElapsed.mean},
                {"frame_elapsed_p95_ms", frameElapsed.p95},
            },
            true,
            direct2DDiagnosticApplicationWaitCount_,
            static_cast<double>(direct2DDiagnosticApplicationWaitTicks_) *
                1000.0 / static_cast<double>(frequency.QuadPart));
    }

    void WaitForGpu()
    {
        if (!commandQueue_ || !fence_)
        {
            return;
        }

        const UINT64 fenceValue = nextFenceValue_++;
        ThrowIfFailed(
            commandQueue_->Signal(fence_.Get(), fenceValue),
            "ID3D12CommandQueue::Signal");
        WaitForFenceValue(fenceValue);
        for (UINT frameIndex = 0; frameIndex < kFrameCount; ++frameIndex)
        {
            ConsumeGpuTimestampForFrame(frameIndex);
        }
    }

    void Shutdown()
    {
        if (commandsSubmitted_ && commandQueue_ && fence_)
        {
            WaitForGpu();
        }

        direct2DBitmap_.Reset();
        direct2DBitmapBrush_.Reset();
        direct2DBitmapTarget_.Reset();
        direct2DTarget_.Reset();
        direct2DFactory_.Reset();
        for (auto& renderTarget : renderTargets_)
        {
            renderTarget.Reset();
        }
        swapChain_.Reset();
        commandList_.Reset();
        for (auto& allocator : commandAllocators_)
        {
            allocator.Reset();
        }
        indexBuffer_.Reset();
        vertexBuffer_.Reset();
        shaderInstanceBuffer_.Reset();
        shaderPipelineState_.Reset();
        shaderRootSignature_.Reset();
        gpuTimestampQueryHeap_.Reset();
        if (gpuTimestampReadback_ && gpuTimestampMapped_ != nullptr)
        {
            D3D12_RANGE writtenRange{0, 0};
            gpuTimestampReadback_->Unmap(0, &writtenRange);
            gpuTimestampMapped_ = nullptr;
        }
        gpuTimestampReadback_.Reset();
        visualReadback_.Reset();
        pipelineState_.Reset();
        rootSignature_.Reset();
        renderTargetHeap_.Reset();
        fence_.Reset();
        commandQueue_.Reset();
        device_.Reset();
        factory_.Reset();

        if (fenceEvent_ != nullptr)
        {
            CloseHandle(fenceEvent_);
            fenceEvent_ = nullptr;
        }
        if (font_ != nullptr)
        {
            DeleteObject(font_);
            font_ = nullptr;
        }
    }

private:
    void StoreCircleThroughputBenchmarkJson()
    {
        if (benchmarkOutputFile_.empty())
        {
            return;
        }
        const bool direct2D = rendererMode_ == RendererMode::Direct2D;
        const CircleThroughputMetrics& metrics = circleThroughputMetrics_;
        const auto validateMetric = [](double value, const char* name) {
            if (!std::isfinite(value))
            {
                throw std::runtime_error(
                    std::string("Non-finite circle-throughput metric: ") + name);
            }
        };
        validateMetric(
            metrics.measurementElapsedMilliseconds,
            "measurement_elapsed_ms");
        validateMetric(
            metrics.circlesAddedPerSecond,
            "circles_added_per_second");
        validateMetric(
            metrics.circlesDrawnIntoCanvasPerSecond,
            "circles_drawn_into_canvas_per_second");
        validateMetric(
            metrics.endToEndElapsedMilliseconds,
            "end_to_end_including_preparation_elapsed_ms");
        validateMetric(
            metrics.circlesAddedEndToEndPerSecond,
            "circles_added_end_to_end_per_second");
        validateMetric(
            metrics.circlesDrawnEndToEndPerSecond,
            "circles_drawn_end_to_end_per_second");
        validateMetric(
            metrics.canvasDrawCommandIssueCpuMilliseconds,
            "canvas_draw_command_issue_cpu_est_ms");
        validateMetric(
            metrics.canvasUpdateEndDrawCpuMilliseconds,
            "canvas_update_enddraw_cpu_est_ms");

        std::ostringstream json;
        const auto writeOptionalMetric = [&json](
                                             bool available,
                                             double value) {
            if (available)
            {
                json << value;
            }
            else
            {
                json << "null";
            }
        };
        json << std::setprecision(10)
             << "{\n"
             << "  \"schema_version\": 1,\n"
             << "  \"benchmark\": \"circle_throughput\",\n"
             << "  \"status\": \"success\",\n"
             << "  \"exit_code\": 0,\n"
             << "  \"renderer\": \"" << RendererModeName(rendererMode_) << "\",\n"
             << "  \"target_type\": \""
             << (direct2D ? "ID2D1HwndRenderTarget" : "D3D12_persistent_canvas")
             << "\",\n"
             << "  \"hardware_acceleration\": \""
             << (direct2D ? "unknown" : "yes") << "\",\n"
             << "  \"circle_count\": " << circleThroughputTargetCount_ << ",\n"
             << "  \"seed\": " << kSeed << ",\n"
             << "  \"scene_hash\": \"0x" << std::hex << std::uppercase
             << circleThroughputSceneHash_ << std::dec << std::nouppercase
             << "\",\n"
             << "  \"batch_size\": " << metrics.batchSize << ",\n"
             << "  \"measurement_elapsed_ms\": "
             << metrics.measurementElapsedMilliseconds << ",\n"
             << "  \"completed_frames\": " << metrics.displayFrames << ",\n"
             << "  \"canvas_update_count\": " << metrics.canvasUpdates << ",\n"
             << "  \"circles_added_to_scene\": "
             << metrics.circlesAddedToScene << ",\n"
             << "  \"circles_drawn_into_canvas\": "
             << metrics.circlesDrawnIntoCanvas << ",\n"
             << "  \"circles_added_per_second\": "
             << metrics.circlesAddedPerSecond << ",\n"
             << "  \"circles_drawn_into_canvas_per_second\": "
             << metrics.circlesDrawnIntoCanvasPerSecond << ",\n"
             << "  \"scene_data_preparation_included_in_throughput_elapsed\": false,\n"
             << "  \"end_to_end_including_preparation_elapsed_ms\": "
             << metrics.endToEndElapsedMilliseconds << ",\n"
             << "  \"circles_added_end_to_end_per_second\": "
             << metrics.circlesAddedEndToEndPerSecond << ",\n"
             << "  \"circles_drawn_end_to_end_per_second\": "
             << metrics.circlesDrawnEndToEndPerSecond << ",\n"
             << "  \"scene_data_preparation_ms\": "
             << metrics.sceneDataPreparationMilliseconds << ",\n"
             << "  \"measurement_scope\": \""
             << (direct2D
                     ? "batch insertion through successful Direct2D EndDraw and display; asynchronous GPU completion is unknown; scene preparation and renderer initialization excluded"
                     : "batch insertion through completed display and final D3D12 fence drain; scene preparation and renderer initialization excluded")
             << "\",\n"
             << "  \"end_to_end_scope\": \"scene data preparation plus measurement scope; renderer initialization excluded\",\n"
             << "  \"scene_addition_elapsed_ms\": "
             << metrics.sceneAdditionElapsedMilliseconds << ",\n"
             << "  \"canvas_completion_elapsed_ms\": "
             << metrics.canvasCompletionElapsedMilliseconds << ",\n"
             << "  \"canvas_completion_semantics\": \""
             << (direct2D
                     ? "successful Direct2D bitmap EndDraw; asynchronous GPU completion is unknown"
                     : "D3D12 submission confirmed complete by fence")
             << "\",\n"
             << "  \"canvas_update_elapsed_ms\": ";
        if (direct2D)
        {
            json << metrics.canvasUpdateElapsedMilliseconds;
        }
        else
        {
            json << "null";
        }
        json << ",\n"
             << "  \"canvas_draw_command_issue_elapsed_ms\": ";
        writeOptionalMetric(
            direct2D, metrics.canvasDrawCommandIssueMilliseconds);
        json << ",\n"
             << "  \"canvas_draw_command_issue_cpu_est_ms\": ";
        writeOptionalMetric(
            direct2D,
            metrics.canvasDrawCommandIssueCpuMilliseconds);
        json << ",\n"
             << "  \"canvas_update_enddraw_elapsed_ms\": ";
        writeOptionalMetric(
            direct2D, metrics.canvasUpdateEndDrawMilliseconds);
        json << ",\n"
             << "  \"canvas_update_enddraw_cpu_est_ms\": ";
        writeOptionalMetric(
            direct2D,
            metrics.canvasUpdateEndDrawCpuMilliseconds);
        json << ",\n"
             << "  \"cpu_time_estimation\": \""
             << (direct2D
                     ? "Direct2D CPU estimates calibrate QueryThreadCycleTime against thread CPU time over the full measurement; they are not GPU time"
                     : "D3D12 stage timings are QPC elapsed times; GPU draw timing is reported separately")
             << "\",\n"
             << "  \"renderer_geometry_preparation_elapsed_ms\": ";
        writeOptionalMetric(
            !direct2D, metrics.rendererGeometryPreparationMilliseconds);
        json << ",\n"
             << "  \"renderer_geometry_upload_elapsed_ms\": ";
        writeOptionalMetric(
            !direct2D, metrics.rendererGeometryUploadMilliseconds);
        json << ",\n"
             << "  \"renderer_command_record_elapsed_ms\": ";
        writeOptionalMetric(
            !direct2D, metrics.rendererCommandRecordMilliseconds);
        json << ",\n"
             << "  \"renderer_command_execute_api_elapsed_ms\": ";
        writeOptionalMetric(
            !direct2D, metrics.rendererCommandExecuteMilliseconds);
        json << ",\n"
             << "  \"gpu_draw_time_available\": "
             << (metrics.gpuDrawTimeAvailable ? "true" : "false") << ",\n"
             << "  \"gpu_draw_ms\": ";
        if (metrics.gpuDrawTimeAvailable)
        {
            json << metrics.d3dGpuDrawMilliseconds;
        }
        else
        {
            json << "null";
        }
        json << ",\n"
             << "  \"gpu_draw_time_source\": \""
             << (metrics.gpuDrawTimeAvailable
                     ? "D3D12 timestamp queries around canvas draw commands"
                     : (direct2D
                            ? "unavailable; Direct2D EndDraw is not GPU completion"
                            : "unavailable; D3D12 timestamp queries were unavailable or invalid"))
             << "\",\n"
             << "  \"bitmap_blit_issue_ms\": ";
        writeOptionalMetric(
            direct2D, metrics.bitmapBlitIssueMilliseconds);
        json << ",\n"
             << "  \"hwnd_enddraw_elapsed_ms\": ";
        writeOptionalMetric(direct2D, metrics.hwndEndDrawMilliseconds);
        json << ",\n"
             << "  \"d3d12_present_api_ms\": ";
        writeOptionalMetric(
            !direct2D, metrics.d3dPresentApiMilliseconds);
        json << ",\n"
             << "  \"d3d12_wait_for_frame_elapsed_ms\": ";
        writeOptionalMetric(
            !direct2D, metrics.d3dFrameFenceWaitMilliseconds);
        json << ",\n"
             << "  \"d3d12_final_fence_drain_ms\": ";
        writeOptionalMetric(
            !direct2D, metrics.d3dFinalFenceDrainMilliseconds);
        json << "\n}\n";
        benchmarkOutputJson_ = json.str();
    }

    void StoreRendererBenchmarkJson(
        UINT measurementMilliseconds,
        std::uint64_t sceneHash,
        UINT64 completedFrames,
        double drawCallsPerFrame,
        bool gpuTimeAvailable,
        double gpuExecutionMillisecondsPerFrame,
        bool scenePreparationAvailable,
        double scenePreparationMilliseconds,
        bool canvasUpdateMetricsAvailable,
        UINT64 newlyRenderedCircles,
        UINT64 canvasUpdateCount,
        double canvasUpdateMilliseconds,
        const std::vector<std::pair<std::string, double>>& timingMetrics,
        bool applicationWaitMetricsAvailable = false,
        UINT64 applicationWaitMessageCount = 0,
        double applicationWaitMessageMilliseconds = 0.0)
    {
        if (benchmarkOutputFile_.empty())
        {
            return;
        }

        const auto validateMetric = [](double value, const char* name) {
            if (!std::isfinite(value))
            {
                throw std::runtime_error(
                    std::string("Non-finite benchmark metric: ") + name);
            }
        };
        validateMetric(drawCallsPerFrame, "draw_calls_per_frame");
        if (gpuTimeAvailable)
        {
            validateMetric(
                gpuExecutionMillisecondsPerFrame,
                "gpu_execution_ms_per_frame");
        }
        if (scenePreparationAvailable)
        {
            validateMetric(
                scenePreparationMilliseconds,
                "scene_data_preparation_ms");
        }
        for (const auto& metric : timingMetrics)
        {
            validateMetric(metric.second, metric.first.c_str());
        }
        if (canvasUpdateMetricsAvailable)
        {
            validateMetric(canvasUpdateMilliseconds, "canvas_update_elapsed_ms");
        }
        if (applicationWaitMetricsAvailable)
        {
            validateMetric(
                applicationWaitMessageMilliseconds,
                "application_wait_message_elapsed_ms");
        }

        std::ostringstream json;
        json << std::setprecision(10)
             << "{\n"
             << "  \"status\": \"success\",\n"
             << "  \"exit_code\": 0,\n"
             << "  \"renderer\": \"" << RendererModeName(rendererMode_) << "\",\n"
             << "  \"target_type\": \""
             << (rendererMode_ == RendererMode::Direct2D
                     ? "ID2D1HwndRenderTarget"
                     : "D3D12_swap_chain")
             << "\",\n"
             << "  \"circle_count\": " << requestedCircleCount_ << ",\n"
             << "  \"seed\": " << kSeed << ",\n"
             << "  \"scene_hash\": \"0x" << std::hex << std::uppercase
             << sceneHash << std::dec << std::nouppercase << "\",\n"
             << "  \"measurement_ms\": " << measurementMilliseconds << ",\n"
             << "  \"completed_frames\": " << completedFrames << ",\n"
             << "  \"draw_calls_per_frame\": " << drawCallsPerFrame << ",\n"
             << "  \"newly_rendered_circles\": ";
        if (canvasUpdateMetricsAvailable)
        {
            json << newlyRenderedCircles;
        }
        else
        {
            json << "null";
        }
        json << ",\n"
             << "  \"canvas_update_count\": ";
        if (canvasUpdateMetricsAvailable)
        {
            json << canvasUpdateCount;
        }
        else
        {
            json << "null";
        }
        json << ",\n"
             << "  \"canvas_update_elapsed_ms\": ";
        if (canvasUpdateMetricsAvailable)
        {
            json << canvasUpdateMilliseconds;
        }
        else
        {
            json << "null";
        }
        json << ",\n"
             << "  \"application_wait_message_count\": ";
        if (applicationWaitMetricsAvailable)
        {
            json << applicationWaitMessageCount;
        }
        else
        {
            json << "null";
        }
        json << ",\n"
             << "  \"application_wait_message_elapsed_ms\": ";
        if (applicationWaitMetricsAvailable)
        {
            json << applicationWaitMessageMilliseconds;
        }
        else
        {
            json << "null";
        }
        json << ",\n"
             << "  \"scene_data_preparation_ms\": ";
        if (scenePreparationAvailable)
        {
            json << scenePreparationMilliseconds;
        }
        else
        {
            json << "null";
        }
        json << ",\n"
             << "  \"gpu_execution_time_available\": "
             << (gpuTimeAvailable ? "true" : "false") << ",\n"
             << "  \"gpu_execution_ms_per_frame\": ";
        if (gpuTimeAvailable)
        {
            json << gpuExecutionMillisecondsPerFrame;
        }
        else
        {
            json << "null";
        }
        json << ",\n"
             << "  \"gpu_time_source\": \""
             << (gpuTimeAvailable
                     ? "D3D12 timestamp queries"
                     : "unavailable; EndDraw is not GPU completion")
             << "\",\n"
             << "  \"timings_ms_per_frame\": {\n";
        for (std::size_t index = 0; index < timingMetrics.size(); ++index)
        {
            json << "    \"" << timingMetrics[index].first << "\": "
                 << timingMetrics[index].second
                 << (index + 1 == timingMetrics.size() ? "\n" : ",\n");
        }
        json << "  }\n}\n";
        benchmarkOutputJson_ = json.str();
    }

    void CreateDirect2DResources()
    {
        ThrowIfFailed(
            D2D1CreateFactory(
                D2D1_FACTORY_TYPE_SINGLE_THREADED,
                IID_PPV_ARGS(direct2DFactory_.GetAddressOf())),
            "D2D1CreateFactory");
        CreateDirect2DTarget();
        RebuildDirect2DBitmapCanvas(true);
    }

    void CreateDirect2DTarget()
    {
        if (width_ == 0 || height_ == 0)
        {
            throw std::runtime_error(
                "Cannot create a Direct2D render target for an empty client area");
        }
        ThrowIfFailed(
            direct2DFactory_->CreateHwndRenderTarget(
                D2D1::RenderTargetProperties(
                    D2D1_RENDER_TARGET_TYPE_DEFAULT),
                D2D1::HwndRenderTargetProperties(
                    window_, D2D1::SizeU(width_, height_)),
                direct2DTarget_.GetAddressOf()),
            "ID2D1Factory::CreateHwndRenderTarget");
    }

    D2D1_COLOR_F Direct2DCanvasClearColor() const
    {
        return requestedCircleCount_ == 0
            ? D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f)
            : D2D1::ColorF(0.08f, 0.30f, 0.72f, 1.0f);
    }

    void DrawDirect2DCircle(const CircleParameters& circle)
    {
        const float canvasWidth = static_cast<float>(canvasWidth_);
        const float clientHeight = static_cast<float>(height_);
        const float centerX = std::clamp(
            circle.normalizedCenterX * canvasWidth,
            circle.outerRadius,
            std::max(circle.outerRadius, canvasWidth - circle.outerRadius));
        const float centerY = std::clamp(
            circle.normalizedCenterY * clientHeight,
            circle.outerRadius,
            std::max(circle.outerRadius, clientHeight - circle.outerRadius));
        const float centerlineRadius = std::max(
            0.0f, circle.outerRadius - circle.thickness * 0.5f);
        direct2DBitmapBrush_->SetColor(D2D1::ColorF(
            circle.red, circle.green, circle.blue, 1.0f));
        direct2DBitmapTarget_->DrawEllipse(
            D2D1::Ellipse(
                D2D1::Point2F(centerX, centerY),
                centerlineRadius,
                centerlineRadius),
            direct2DBitmapBrush_.Get(),
            circle.thickness);
    }

    void DrawDirect2DShape(const DrawingShape& shape)
    {
        direct2DBitmapBrush_->SetColor(D2D1::ColorF(
            static_cast<float>(GetRValue(shape.color)) / 255.0f,
            static_cast<float>(GetGValue(shape.color)) / 255.0f,
            static_cast<float>(GetBValue(shape.color)) / 255.0f,
            1.0f));
        if (shape.circle)
        {
            const float centerX =
                (static_cast<float>(shape.x1) + shape.x2) * 0.5f;
            const float centerY =
                (static_cast<float>(shape.y1) + shape.y2) * 0.5f;
            const float radius = std::max(
                0.0f,
                static_cast<float>(shape.x2 - shape.x1) * 0.5f -
                    static_cast<float>(shape.thickness) * 0.5f);
            direct2DBitmapTarget_->DrawEllipse(
                D2D1::Ellipse(
                    D2D1::Point2F(centerX, centerY), radius, radius),
                direct2DBitmapBrush_.Get(),
                static_cast<float>(shape.thickness));
        }
        else
        {
            direct2DBitmapTarget_->DrawLine(
                D2D1::Point2F(
                    static_cast<float>(shape.x1),
                    static_cast<float>(shape.y1)),
                D2D1::Point2F(
                    static_cast<float>(shape.x2),
                    static_cast<float>(shape.y2)),
                direct2DBitmapBrush_.Get(),
                static_cast<float>(shape.thickness));
        }
    }

    void RecordDirect2DCanvasUpdate(
        UINT64 elapsedTicks,
        UINT64 newlyRenderedCircles)
    {
        ++direct2DCanvasUpdateCount_;
        direct2DCanvasUpdateTicks_ += elapsedTicks;
        direct2DNewlyRenderedCircleCount_ += newlyRenderedCircles;
        if (circleThroughputBenchmarkActive_)
        {
            ++circleThroughputMetrics_.canvasUpdates;
            circleThroughputMetrics_.circlesDrawnIntoCanvas +=
                newlyRenderedCircles;
            AddCircleThroughputElapsed(
                circleThroughputMetrics_.canvasUpdateElapsedMilliseconds,
                elapsedTicks);
        }
        if (diagnosticsEnabled_)
        {
            ++direct2DDiagnosticCanvasUpdateCount_;
            direct2DDiagnosticCanvasUpdateTicks_ += elapsedTicks;
            direct2DDiagnosticNewCircleCount_ += newlyRenderedCircles;
        }
    }

    void RebuildDirect2DBitmapCanvas(bool countInitialSceneAsNew)
    {
        LARGE_INTEGER updateStart{};
        if (!QueryPerformanceCounter(&updateStart))
        {
            throw std::runtime_error(
                "QueryPerformanceCounter failed before Direct2D canvas rebuild");
        }
        const std::size_t firstNewShape = canvasNeedsClear_
            ? 0
            : std::min(direct2DDrawnShapeCount_, drawingShapes_.size());
        UINT64 newlyRenderedCircles =
            countInitialSceneAsNew && requestedCircleCount_ != 0
                ? static_cast<UINT64>(circleParameters_.size())
                : 0;
        if (requestedCircleCount_ == 0)
        {
            for (std::size_t index = firstNewShape;
                 index < drawingShapes_.size();
                 ++index)
            {
                if (drawingShapes_[index].circle)
                {
                    ++newlyRenderedCircles;
                }
            }
        }

        for (UINT attempt = 0; attempt < 2; ++attempt)
        {
            direct2DBitmap_.Reset();
            direct2DBitmapBrush_.Reset();
            direct2DBitmapTarget_.Reset();
            ThrowIfFailed(
                direct2DTarget_->CreateCompatibleRenderTarget(
                    direct2DBitmapTarget_.GetAddressOf()),
                "ID2D1RenderTarget::CreateCompatibleRenderTarget");
            ThrowIfFailed(
                direct2DBitmapTarget_->CreateSolidColorBrush(
                    D2D1::ColorF(0.0f, 0.0f, 0.0f),
                    direct2DBitmapBrush_.GetAddressOf()),
                "ID2D1BitmapRenderTarget::CreateSolidColorBrush");

            direct2DBitmapTarget_->BeginDraw();
            direct2DBitmapTarget_->Clear(Direct2DCanvasClearColor());
            if (requestedCircleCount_ != 0)
            {
                for (const CircleParameters& circle : circleParameters_)
                {
                    DrawDirect2DCircle(circle);
                }
            }
            else
            {
                for (const DrawingShape& shape : drawingShapes_)
                {
                    DrawDirect2DShape(shape);
                }
            }
            const HRESULT result = direct2DBitmapTarget_->EndDraw();
            if (result == D2DERR_RECREATE_TARGET)
            {
                direct2DBitmap_.Reset();
                direct2DBitmapBrush_.Reset();
                direct2DBitmapTarget_.Reset();
                direct2DTarget_.Reset();
                if (attempt == 0)
                {
                    CreateDirect2DTarget();
                    continue;
                }
                throw std::runtime_error(
                    "Direct2D bitmap canvas was lost again after recreation");
            }
            ThrowIfFailed(
                result,
                "ID2D1BitmapRenderTarget::EndDraw(rebuild)");
            ThrowIfFailed(
                direct2DBitmapTarget_->GetBitmap(
                    direct2DBitmap_.GetAddressOf()),
                "ID2D1BitmapRenderTarget::GetBitmap");
            break;
        }

        direct2DDrawnShapeCount_ = drawingShapes_.size();
        canvasNeedsClear_ = false;
        canvasDirty_ = false;
        LARGE_INTEGER updateStop{};
        if (!QueryPerformanceCounter(&updateStop))
        {
            throw std::runtime_error(
                "QueryPerformanceCounter failed after Direct2D canvas rebuild");
        }
        RecordDirect2DCanvasUpdate(
            static_cast<UINT64>(updateStop.QuadPart - updateStart.QuadPart),
            newlyRenderedCircles);
    }

    void UpdateDirect2DCanvas()
    {
        if (requestedCircleCount_ != 0)
        {
            if (canvasNeedsClear_)
            {
                RebuildDirect2DBitmapCanvas(false);
            }
            return;
        }

        const bool clearCanvas = canvasNeedsClear_;
        const std::size_t firstShape = clearCanvas
            ? 0
            : std::min(direct2DDrawnShapeCount_, drawingShapes_.size());
        const std::size_t endShape = drawingShapes_.size();
        if (!clearCanvas && firstShape == endShape)
        {
            return;
        }

        LARGE_INTEGER updateStart{};
        if (!QueryPerformanceCounter(&updateStart))
        {
            throw std::runtime_error(
                "QueryPerformanceCounter failed before Direct2D canvas update");
        }
        UINT64 newlyRenderedCircles = 0;
        for (std::size_t index = firstShape; index < endShape; ++index)
        {
            if (drawingShapes_[index].circle)
            {
                ++newlyRenderedCircles;
            }
        }

        direct2DBitmapTarget_->BeginDraw();
        if (clearCanvas)
        {
            direct2DBitmapTarget_->Clear(Direct2DCanvasClearColor());
        }
        LARGE_INTEGER drawIssueStart{};
        ULONGLONG drawIssueCycleStart = 0;
        if (circleThroughputBenchmarkActive_ &&
            (!QueryPerformanceCounter(&drawIssueStart) ||
             !QueryThreadCycleTime(
                 GetCurrentThread(), &drawIssueCycleStart)))
        {
            throw std::runtime_error(
                "Could not start Direct2D canvas draw-command timing");
        }
        for (std::size_t index = firstShape; index < endShape; ++index)
        {
            DrawDirect2DShape(drawingShapes_[index]);
        }
        LARGE_INTEGER drawIssueStop{};
        ULONGLONG drawIssueCycleStop = 0;
        if (circleThroughputBenchmarkActive_ &&
            (!QueryPerformanceCounter(&drawIssueStop) ||
             !QueryThreadCycleTime(
                 GetCurrentThread(), &drawIssueCycleStop)))
        {
            throw std::runtime_error(
                "Could not stop Direct2D canvas draw-command timing");
        }
        LARGE_INTEGER canvasEndDrawStart{};
        ULONGLONG canvasEndDrawCycleStart = 0;
        if (circleThroughputBenchmarkActive_ &&
            (!QueryPerformanceCounter(&canvasEndDrawStart) ||
             !QueryThreadCycleTime(
                 GetCurrentThread(), &canvasEndDrawCycleStart)))
        {
            throw std::runtime_error(
                "Could not start Direct2D canvas EndDraw timing");
        }
        const HRESULT result = direct2DBitmapTarget_->EndDraw();
        LARGE_INTEGER canvasEndDrawStop{};
        ULONGLONG canvasEndDrawCycleStop = 0;
        if (circleThroughputBenchmarkActive_ &&
            (!QueryPerformanceCounter(&canvasEndDrawStop) ||
             !QueryThreadCycleTime(
                 GetCurrentThread(), &canvasEndDrawCycleStop)))
        {
            throw std::runtime_error(
                "Could not stop Direct2D canvas EndDraw timing");
        }
        if (result == D2DERR_RECREATE_TARGET)
        {
            direct2DBitmap_.Reset();
            direct2DBitmapBrush_.Reset();
            direct2DBitmapTarget_.Reset();
            direct2DTarget_.Reset();
            CreateDirect2DTarget();
            RebuildDirect2DBitmapCanvas(false);
            return;
        }
        ThrowIfFailed(result, "ID2D1BitmapRenderTarget::EndDraw(update)");

        direct2DDrawnShapeCount_ = endShape;
        canvasNeedsClear_ = false;
        canvasDirty_ = false;
        if (circleThroughputBenchmarkActive_)
        {
            AddCircleThroughputElapsed(
                circleThroughputMetrics_.canvasDrawCommandIssueMilliseconds,
                static_cast<UINT64>(
                    drawIssueStop.QuadPart - drawIssueStart.QuadPart));
            AddCircleThroughputElapsed(
                circleThroughputMetrics_.canvasUpdateEndDrawMilliseconds,
                static_cast<UINT64>(
                    canvasEndDrawStop.QuadPart - canvasEndDrawStart.QuadPart));
            circleThroughputMetrics_.canvasDrawCommandIssueCycles +=
                drawIssueCycleStop - drawIssueCycleStart;
            circleThroughputMetrics_.canvasUpdateEndDrawCycles +=
                canvasEndDrawCycleStop - canvasEndDrawCycleStart;
        }
        LARGE_INTEGER updateStop{};
        if (!QueryPerformanceCounter(&updateStop))
        {
            throw std::runtime_error(
                "QueryPerformanceCounter failed after Direct2D canvas update");
        }
        RecordDirect2DCanvasUpdate(
            static_cast<UINT64>(updateStop.QuadPart - updateStart.QuadPart),
            newlyRenderedCircles);
    }

    UINT64 Direct2DSceneCircleCount() const
    {
        if (requestedCircleCount_ != 0)
        {
            return static_cast<UINT64>(circleParameters_.size());
        }
        return static_cast<UINT64>(std::count_if(
            drawingShapes_.begin(),
            drawingShapes_.end(),
            [](const DrawingShape& shape) { return shape.circle; }));
    }

    void ResetDirect2DDiagnosticThreadBaseline()
    {
        FILETIME creation{}, exit{}, kernel{}, user{};
        if (!GetThreadTimes(
                GetCurrentThread(), &creation, &exit, &kernel, &user) ||
            !QueryThreadCycleTime(
                GetCurrentThread(),
                &direct2DDiagnosticThreadCycleBaseline_))
        {
            throw std::runtime_error(
                "Could not initialize Direct2D diagnostic CPU counters");
        }
        direct2DDiagnosticThreadKernelBaseline_ = kernel;
        direct2DDiagnosticThreadUserBaseline_ = user;
        diagnosticWindowStartMs_ = GetTickCount64();
    }

    void PrintRendererDiagnostics()
    {
        std::cout << "[diag] renderer=" << RendererModeName(rendererMode_);
        if (rendererMode_ == RendererMode::Direct2D)
        {
            ResetDirect2DDiagnosticThreadBaseline();
            std::cout << " target_type=ID2D1HwndRenderTarget"
                      << " requested_target_type=default"
                      << " hardware_acceleration=unknown"
                      << " gpu_adapter=unknown gpu_selection=not_supported"
                      << " requested_gpu=" << GpuSelectionLabel(gpuSelection_)
                      << " adapter_selection_status=not_exposed_by_hwnd_target\n"
                      << std::flush;
            return;
        }

        std::cout << " target_type=D3D12_swap_chain"
                  << " hardware_acceleration=yes gpu_selection="
                  << GpuSelectionLabel(gpuSelection_)
                  << " adapter_selection_status=verified\n" << std::flush;
    }

    void PrintLegacyMemoryDiagnostics()
    {
        const auto byteCount = [](std::size_t count, std::size_t elementSize,
                                  std::size_t& bytes) {
            if (elementSize != 0 &&
                count > std::numeric_limits<std::size_t>::max() / elementSize)
            {
                return false;
            }
            bytes = count * elementSize;
            return true;
        };
        const auto printVectorBytes = [&byteCount](
                                          const char* name,
                                          std::size_t size,
                                          std::size_t capacity,
                                          std::size_t elementSize) {
            std::size_t sizeBytes = 0;
            std::size_t capacityBytes = 0;
            std::cout << "[diag][legacy-memory] " << name << "_size_bytes=";
            if (byteCount(size, elementSize, sizeBytes))
            {
                std::cout << sizeBytes;
            }
            else
            {
                std::cout << "unavailable(overflow)";
            }
            std::cout << ' ' << name << "_capacity_bytes=";
            if (byteCount(capacity, elementSize, capacityBytes))
            {
                std::cout << capacityBytes;
            }
            else
            {
                std::cout << "unavailable(overflow)";
            }
            std::cout << '\n';
        };

        printVectorBytes("vertices", vertices_.size(), vertices_.capacity(),
                         sizeof(Vertex));
        printVectorBytes("indices", indices_.size(), indices_.capacity(),
                         sizeof(std::uint16_t));
        printVectorBytes("circleDraws", circleDraws_.size(),
                         circleDraws_.capacity(), sizeof(CircleDraw));
        printVectorBytes("drawingShapes", drawingShapes_.size(),
                         drawingShapes_.capacity(), sizeof(DrawingShape));
        printVectorBytes("shapeCircleInstanceIndices",
                         shapeCircleInstanceIndices_.size(),
                         shapeCircleInstanceIndices_.capacity(), sizeof(UINT));
        std::cout << "[diag][legacy-memory] current_vertex_buffer_capacity_bytes=";
        if (vertexBuffer_)
        {
            std::cout << vertexBufferCapacityBytes_;
        }
        else
        {
            std::cout << "unavailable(no buffer)";
        }
        std::cout << " current_index_buffer_capacity_bytes=";
        if (indexBuffer_)
        {
            std::cout << indexBufferCapacityBytes_;
        }
        else
        {
            std::cout << "unavailable(no buffer)";
        }
        std::cout << '\n';

        UINT64 retiredVertexBytes = 0;
        UINT64 retiredIndexBytes = 0;
        bool retiredVertexBytesAvailable = true;
        bool retiredIndexBytesAvailable = true;
        const auto addRetiredCapacity = [](UINT64 capacity, UINT64& total) {
            if (capacity > std::numeric_limits<UINT64>::max() - total)
            {
                return false;
            }
            total += capacity;
            return true;
        };
        for (const RetiredGeometryBuffers& buffers : retiredGeometryBuffers_)
        {
            if (buffers.vertices &&
                !addRetiredCapacity(buffers.vertices->GetDesc().Width,
                                    retiredVertexBytes))
            {
                retiredVertexBytesAvailable = false;
            }
            if (buffers.indices &&
                !addRetiredCapacity(buffers.indices->GetDesc().Width,
                                    retiredIndexBytes))
            {
                retiredIndexBytesAvailable = false;
            }
        }
        std::cout << "[diag][legacy-memory] retired_buffer_entries="
                  << retiredGeometryBuffers_.size()
                  << " retired_vertex_capacity_bytes=";
        if (retiredVertexBytesAvailable)
        {
            std::cout << retiredVertexBytes;
        }
        else
        {
            std::cout << "unavailable(overflow)";
        }
        std::cout << " retired_index_capacity_bytes=";
        if (retiredIndexBytesAvailable)
        {
            std::cout << retiredIndexBytes;
        }
        else
        {
            std::cout << "unavailable(overflow)";
        }
        std::cout << '\n';

        PROCESS_MEMORY_COUNTERS_EX memory{};
        memory.cb = sizeof(memory);
        if (GetProcessMemoryInfo(
                GetCurrentProcess(),
                reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),
                sizeof(memory)))
        {
            std::cout << "[diag][legacy-memory] process_private_usage_bytes="
                      << memory.PrivateUsage << '\n';
        }
        else
        {
            std::cout << "[diag][legacy-memory] process_private_usage_bytes="
                         "unavailable(GetProcessMemoryInfo error "
                      << GetLastError() << ")\n";
        }
        std::cout << std::flush;
    }

    void RenderDirect2DFrame()
    {
        UpdateDirect2DCanvas();
        const bool measure = direct2DBenchmarkActive_ || diagnosticsEnabled_ ||
            circleThroughputBenchmarkActive_;
        LARGE_INTEGER frameStart{};
        if (measure && !QueryPerformanceCounter(&frameStart))
        {
            throw std::runtime_error(
                "QueryPerformanceCounter failed before Direct2D frame");
        }

        LARGE_INTEGER issueStart{};
        ULONGLONG issueCycleStart = 0;
        LARGE_INTEGER issueStop{};
        ULONGLONG issueCycleStop = 0;
        LARGE_INTEGER endDrawStart{};
        ULONGLONG endDrawCycleStart = 0;
        LARGE_INTEGER endDrawStop{};
        ULONGLONG endDrawCycleStop = 0;
        UINT drawCalls = 0;
        bool targetRecreated = false;
        for (UINT attempt = 0; attempt < 2; ++attempt)
        {
            if (!direct2DTarget_)
            {
                CreateDirect2DTarget();
                RebuildDirect2DBitmapCanvas(false);
            }
            if (measure &&
                (!QueryPerformanceCounter(&issueStart) ||
                 !QueryThreadCycleTime(
                     GetCurrentThread(), &issueCycleStart)))
            {
                throw std::runtime_error(
                    "Could not start Direct2D draw-issue timing");
            }

            direct2DTarget_->BeginDraw();
            const D2D1_SIZE_F bitmapSize =
                direct2DBitmapTarget_->GetSize();
            const D2D1_RECT_F destination = D2D1::RectF(
                0.0f, 0.0f, bitmapSize.width, bitmapSize.height);
            direct2DTarget_->DrawBitmap(
                direct2DBitmap_.Get(),
                destination,
                1.0f,
                D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
            drawCalls = 1;

            if (measure &&
                (!QueryPerformanceCounter(&issueStop) ||
                 !QueryThreadCycleTime(
                     GetCurrentThread(), &issueCycleStop) ||
                 !QueryPerformanceCounter(&endDrawStart) ||
                 !QueryThreadCycleTime(
                     GetCurrentThread(), &endDrawCycleStart)))
            {
                throw std::runtime_error(
                    "Could not start Direct2D EndDraw timing");
            }
            const HRESULT endDrawResult = direct2DTarget_->EndDraw();
            if (measure &&
                (!QueryPerformanceCounter(&endDrawStop) ||
                 !QueryThreadCycleTime(
                     GetCurrentThread(), &endDrawCycleStop)))
            {
                throw std::runtime_error(
                    "Could not stop Direct2D EndDraw timing");
            }
            if (endDrawResult == D2DERR_RECREATE_TARGET)
            {
                direct2DBitmap_.Reset();
                direct2DBitmapBrush_.Reset();
                direct2DBitmapTarget_.Reset();
                direct2DTarget_.Reset();
                targetRecreated = true;
                if (attempt == 0)
                {
                    CreateDirect2DTarget();
                    RebuildDirect2DBitmapCanvas(false);
                    continue;
                }
                throw std::runtime_error(
                    "Direct2D render target was lost again after recreation");
            }
            ThrowIfFailed(endDrawResult, "ID2D1RenderTarget::EndDraw");
            break;
        }

        if (circleThroughputBenchmarkActive_)
        {
            ++circleThroughputMetrics_.displayFrames;
            AddCircleThroughputElapsed(
                circleThroughputMetrics_.bitmapBlitIssueMilliseconds,
                static_cast<UINT64>(issueStop.QuadPart - issueStart.QuadPart));
            AddCircleThroughputElapsed(
                circleThroughputMetrics_.hwndEndDrawMilliseconds,
                static_cast<UINT64>(
                    endDrawStop.QuadPart - endDrawStart.QuadPart));
        }

        LARGE_INTEGER frameStop{};
        if (measure && !QueryPerformanceCounter(&frameStop))
        {
            throw std::runtime_error(
                "QueryPerformanceCounter failed after Direct2D frame");
        }
        if (measure)
        {
            Direct2DFrameSample sample;
            sample.drawIssueCycles = issueCycleStop - issueCycleStart;
            sample.endDrawCycles = endDrawCycleStop - endDrawCycleStart;
            sample.drawIssueElapsedTicks = static_cast<UINT64>(
                issueStop.QuadPart - issueStart.QuadPart);
            sample.endDrawElapsedTicks = static_cast<UINT64>(
                endDrawStop.QuadPart - endDrawStart.QuadPart);
            sample.frameElapsedTicks = static_cast<UINT64>(
                frameStop.QuadPart - frameStart.QuadPart);
            sample.drawCalls = drawCalls;
            if (direct2DBenchmarkActive_ &&
                frameStop.QuadPart < rendererBenchmarkMeasurementEnd_.QuadPart)
            {
                direct2DBenchmarkSamples_.push_back(sample);
            }
            if (diagnosticsEnabled_ && !direct2DBenchmarkActive_)
            {
                diagnosticFrameCount_++;
                diagnosticRecordTicks_ += sample.drawIssueElapsedTicks;
                diagnosticPresentTicks_ += sample.endDrawElapsedTicks;
                diagnosticSignalTicks_ += sample.frameElapsedTicks;
                direct2DDiagnosticEndDrawCycles_ += sample.endDrawCycles;
                if (targetRecreated)
                {
                    std::cout << "[diag] direct2d_target_recreated=1\n";
                }
                ReportDirect2DDiagnosticsIfDue();
            }
        }
        if (interactiveLayout_)
        {
            renderedInSpeedWindow_ += pendingGeneratedShapesForPresent_;
            pendingGeneratedShapesForPresent_ = 0;
            UpdateDrawingRateWindow();
        }
    }

    void ReportDirect2DDiagnosticsIfDue()
    {
        const ULONGLONG now = GetTickCount64();
        const ULONGLONG elapsedMs = now - diagnosticWindowStartMs_;
        if (elapsedMs < 1000)
        {
            return;
        }

        const double elapsedSeconds = static_cast<double>(elapsedMs) / 1000.0;
        const double ticksToMs = 1000.0 /
            static_cast<double>(diagnosticFrequency_.QuadPart);
        const double frameCount = static_cast<double>(diagnosticFrameCount_);
        FILETIME creation{}, exit{}, kernel{}, user{};
        ULONGLONG threadCyclesStop = 0;
        if (!GetThreadTimes(
                GetCurrentThread(), &creation, &exit, &kernel, &user) ||
            !QueryThreadCycleTime(
                GetCurrentThread(), &threadCyclesStop))
        {
            throw std::runtime_error(
                "Could not read Direct2D diagnostic CPU counters");
        }
        const ULONGLONG startCpu100ns =
            FileTimeValue(direct2DDiagnosticThreadKernelBaseline_) +
            FileTimeValue(direct2DDiagnosticThreadUserBaseline_);
        const ULONGLONG stopCpu100ns =
            FileTimeValue(kernel) + FileTimeValue(user);
        if (stopCpu100ns < startCpu100ns ||
            threadCyclesStop < direct2DDiagnosticThreadCycleBaseline_)
        {
            throw std::runtime_error(
                "Direct2D diagnostic CPU counters moved backwards");
        }
        const ULONGLONG threadCpu100ns = stopCpu100ns - startCpu100ns;
        const ULONGLONG threadCycles =
            threadCyclesStop - direct2DDiagnosticThreadCycleBaseline_;
        const double cpuMsPerCycle = threadCycles == 0
            ? 0.0
            : (static_cast<double>(threadCpu100ns) / 10000.0) /
                  static_cast<double>(threadCycles);
        const double endDrawCpuMsPerFrame = frameCount == 0.0
            ? 0.0
            : static_cast<double>(direct2DDiagnosticEndDrawCycles_) *
                  cpuMsPerCycle / frameCount;
        std::cout << std::fixed << std::setprecision(2)
                  << "[diag] renderer=direct2d"
                  << " target_type=ID2D1HwndRenderTarget"
                  << " requested_target_type=default"
                  << " hardware_acceleration=unknown"
                  << " gpu_adapter=unknown"
                  << " frames=" << diagnosticFrameCount_
                  << " completed_enddraw_frames/s="
                  << frameCount / elapsedSeconds
                  << " scene_circles=" << Direct2DSceneCircleCount()
                  << " newly_rendered_circles/s="
                  << static_cast<double>(direct2DDiagnosticNewCircleCount_) /
                         elapsedSeconds
                  << " canvas_updates="
                  << direct2DDiagnosticCanvasUpdateCount_
                  << " canvas_update_elapsed_ms/update="
                  << (direct2DDiagnosticCanvasUpdateCount_ == 0
                          ? 0.0
                          : static_cast<double>(
                                direct2DDiagnosticCanvasUpdateTicks_) *
                                ticksToMs /
                                static_cast<double>(
                                    direct2DDiagnosticCanvasUpdateCount_))
                  << " bitmap_blit_elapsed_ms/frame="
                  << (frameCount == 0.0
                          ? 0.0
                          : static_cast<double>(diagnosticRecordTicks_) *
                                ticksToMs / frameCount)
                  << " enddraw_cpu_est_ms/frame="
                  << endDrawCpuMsPerFrame
                  << " enddraw_elapsed_ms/frame="
                  << (frameCount == 0.0
                          ? 0.0
                          : static_cast<double>(diagnosticPresentTicks_) *
                                ticksToMs / frameCount)
                  << " frame_elapsed_ms/frame="
                  << (frameCount == 0.0
                          ? 0.0
                          : static_cast<double>(diagnosticSignalTicks_) *
                                ticksToMs / frameCount)
                  << " application_wait_message_calls="
                  << direct2DDiagnosticApplicationWaitCount_
                  << " application_wait_message_elapsed_ms="
                  << static_cast<double>(
                         direct2DDiagnosticApplicationWaitTicks_) *
                         ticksToMs
                  << " gpu_time=unavailable\n" << std::flush;

        diagnosticWindowStartMs_ = now;
        diagnosticFrameCount_ = 0;
        diagnosticRecordTicks_ = 0;
        diagnosticPresentTicks_ = 0;
        diagnosticSignalTicks_ = 0;
        direct2DDiagnosticEndDrawCycles_ = 0;
        direct2DDiagnosticApplicationWaitCount_ = 0;
        direct2DDiagnosticApplicationWaitTicks_ = 0;
        direct2DDiagnosticNewCircleCount_ = 0;
        direct2DDiagnosticCanvasUpdateCount_ = 0;
        direct2DDiagnosticCanvasUpdateTicks_ = 0;
        direct2DDiagnosticThreadKernelBaseline_ = kernel;
        direct2DDiagnosticThreadUserBaseline_ = user;
        direct2DDiagnosticThreadCycleBaseline_ = threadCyclesStop;
    }

    void ClearDrawing()
    {
        StopRandomDrawing();
        std::vector<DrawingShape>().swap(drawingShapes_);
        lastUploadedVertexCount_ = 0;
        lastUploadedIndexCount_ = 0;
        pendingPoint_ = false;
        objectCount_ = 0;
        objectMemoryBytes_ = 0;
        actualObjectsPerSecond_ = 0;
        generatedInSpeedWindow_ = 0;
        renderedInSpeedWindow_ = 0;
        pendingGeneratedShapesForPresent_ = 0;
        generatedShapeCountThisRun_ = 0;
        slowWindowCount_ = 0;
        fastWindowCount_ = 0;
        speedWindowStartMs_ = GetTickCount64();
        canvasRenderedShapeCount_ = 0;
        canvasNeedsClear_ = true;
        canvasDirty_ = true;
        RebuildInteractiveGeometry();
        UpdateInformationPanel();
    }

    static const char* TimerModeName(DrawingTimerMode mode)
    {
        return mode == DrawingTimerMode::WindowTimer ? "WM_TIMER" : "QPC";
    }

    static const char* RendererModeName(RendererMode mode)
    {
        switch (mode)
        {
        case RendererMode::Legacy: return "legacy";
        case RendererMode::Shader: return "shader";
        case RendererMode::Direct2D: return "direct2d";
        }
        return "unknown";
    }

    static const char* PipelineScenarioName(PipelineComparisonScenario scenario)
    {
        switch (scenario)
        {
        case PipelineComparisonScenario::RendererIdle: return "A-renderer-idle";
        case PipelineComparisonScenario::CpuOnlyAdd: return "B-cpu-only-add";
        case PipelineComparisonScenario::FullD3D12: return "C-full-d3d12";
        }
        return "unknown";
    }

    static ULONGLONG FileTimeValue(const FILETIME& value)
    {
        ULARGE_INTEGER converted{};
        converted.LowPart = value.dwLowDateTime;
        converted.HighPart = value.dwHighDateTime;
        return converted.QuadPart;
    }

    void PrintPipelineComparisonResult()
    {
        const double seconds = static_cast<double>(
            timerComparisonMeasurementEnd_.QuadPart - timerComparisonMeasurementStart_.QuadPart) /
            static_cast<double>(timerComparisonFrequency_.QuadPart);
        const double objectRate = seconds > 0.0
            ? static_cast<double>(timerComparisonAddedCount_) / seconds : 0.0;
        const ULONGLONG cpu100ns =
            FileTimeValue(pipelineCpuEndKernel_) + FileTimeValue(pipelineCpuEndUser_) -
            FileTimeValue(pipelineCpuStartKernel_) - FileTimeValue(pipelineCpuStartUser_);
        std::vector<LONGLONG> sorted = pipelineObjectElapsedTicks_;
        std::sort(sorted.begin(), sorted.end());
        std::vector<ULONGLONG> sortedCycles = pipelineObjectCycles_;
        std::sort(sortedCycles.begin(), sortedCycles.end());
        const auto qpcTicksToMs = [this](LONGLONG ticks) {
            return static_cast<double>(ticks) * 1000.0 /
                static_cast<double>(timerComparisonFrequency_.QuadPart);
        };
        const double meanMs = sorted.empty() ? 0.0 :
            qpcTicksToMs(static_cast<LONGLONG>(pipelineObjectElapsedTickSum_)) /
                static_cast<double>(sorted.size());
        const double medianMs = sorted.empty() ? 0.0 :
            (sorted.size() % 2 == 0
                ? qpcTicksToMs(sorted[sorted.size() / 2 - 1] + sorted[sorted.size() / 2]) * 0.5
                : qpcTicksToMs(sorted[sorted.size() / 2]));
        const std::size_t p95Index = sorted.empty() ? 0 : std::min(
            sorted.size() - 1,
            static_cast<std::size_t>(std::ceil(0.95 * sorted.size())) - 1);
        const double p95Ms = sorted.empty() ? 0.0 : qpcTicksToMs(sorted[p95Index]);
        const double uiCpuMsPerSecond = (static_cast<double>(cpu100ns) / 10000.0) / seconds;
        const ULONGLONG threadCycleDelta = pipelineThreadCyclesEnd_ - pipelineThreadCyclesStart_;
        const double cpuMsPerCycle = threadCycleDelta == 0
            ? 0.0
            : (static_cast<double>(cpu100ns) / 10000.0) /
                static_cast<double>(threadCycleDelta);
        const auto cyclesToCpuMs = [cpuMsPerCycle](ULONGLONG cycles) {
            return static_cast<double>(cycles) * cpuMsPerCycle;
        };
        const double addCpuTotalMs = cyclesToCpuMs(pipelineObjectCycleSum_);
        const double addCpuMeanMs = sortedCycles.empty() ? 0.0 :
            addCpuTotalMs / static_cast<double>(sortedCycles.size());
        const double addCpuMedianMs = sortedCycles.empty() ? 0.0 :
            (sortedCycles.size() % 2 == 0
                ? cyclesToCpuMs(sortedCycles[sortedCycles.size() / 2 - 1] +
                                sortedCycles[sortedCycles.size() / 2]) * 0.5
                : cyclesToCpuMs(sortedCycles[sortedCycles.size() / 2]));
        const std::size_t cyclesP95Index = sortedCycles.empty() ? 0 : std::min(
            sortedCycles.size() - 1,
            static_cast<std::size_t>(std::ceil(0.95 * sortedCycles.size())) - 1);
        const double addCpuP95Ms = sortedCycles.empty() ? 0.0 :
            cyclesToCpuMs(sortedCycles[cyclesP95Index]);
        std::cout << std::fixed << std::setprecision(3)
                  << "[pipeline-test] result renderer=" << RendererModeName(rendererMode_)
                  << " scenario=" << PipelineScenarioName(pipelineScenario_)
                  << " target_ms=" << drawIntervalMs_
                  << " measured_ms=" << seconds * 1000.0
                  << " objects=" << timerComparisonAddedCount_
                  << " objects_per_s=" << objectRate
                  << " handler_elapsed_total_ms=" << qpcTicksToMs(static_cast<LONGLONG>(pipelineObjectElapsedTickSum_))
                  << " handler_elapsed_mean_ms=" << meanMs
                  << " handler_elapsed_median_ms=" << medianMs
                  << " handler_elapsed_p95_ms=" << p95Ms
                  << " add_cpu_total_ms_est=" << addCpuTotalMs
                  << " add_cpu_mean_ms_est=" << addCpuMeanMs
                  << " add_cpu_median_ms_est=" << addCpuMedianMs
                  << " add_cpu_p95_ms_est=" << addCpuP95Ms
                  << " geometry_prep_total_ms=" << qpcTicksToMs(
                         static_cast<LONGLONG>(pipelineGeometryPrepTicks_))
                  << " geometry_prep_mean_ms=" << (timerComparisonAddedCount_ == 0
                         ? 0.0
                         : qpcTicksToMs(static_cast<LONGLONG>(pipelineGeometryPrepTicks_)) /
                               static_cast<double>(timerComparisonAddedCount_))
                  << " data_upload_total_ms=" << qpcTicksToMs(
                         static_cast<LONGLONG>(pipelineGeometryUploadTicks_))
                  << " data_upload_mean_ms=" << (timerComparisonAddedCount_ == 0
                         ? 0.0
                         : qpcTicksToMs(static_cast<LONGLONG>(pipelineGeometryUploadTicks_)) /
                               static_cast<double>(timerComparisonAddedCount_))
                  << " gpu_ms_per_draw_update="
                  << (gpuTimestampFrameCount_ == 0 || gpuTimestampFrequency_ == 0
                      ? 0.0
                      : static_cast<double>(gpuTimestampTicksTotal_) * 1000.0 /
                            static_cast<double>(gpuTimestampFrequency_) /
                            static_cast<double>(gpuTimestampFrameCount_))
                  << " gpu_timestamped_draw_updates=" << gpuTimestampFrameCount_
                  << " ui_thread_cpu_ms_per_s=" << uiCpuMsPerSecond
                  << " present_returns_per_s=" << static_cast<double>(pipelinePresentCount_) / seconds
                  << " skipped_qpc_deadlines=" << qpcSkippedDeadlines_ << '\n' << std::flush;
    }

    void RecordTimerComparisonAddition(LONGLONG completedTicks)
    {
        if (timerComparisonLastAdditionTicks_ != 0)
        {
            timerComparisonIntervalsTicks_.push_back(
                completedTicks - timerComparisonLastAdditionTicks_);
        }
        timerComparisonLastAdditionTicks_ = completedTicks;
        ++timerComparisonAddedCount_;
    }

    void StartTimerComparisonCase()
    {
        ClearDrawing();
        randomGenerator_.seed(kSeed);
        const std::size_t intervalIndex = pipelineComparisonActive_
            ? timerComparisonCaseIndex_ % 3
            : timerComparisonCaseIndex_ / 2;
        drawIntervalMs_ = kTimerComparisonIntervalsMs[intervalIndex];
        if (pipelineComparisonActive_)
        {
            const std::size_t scenarioIndex = timerComparisonCaseIndex_ / 3;
            pipelineScenario_ = static_cast<PipelineComparisonScenario>(scenarioIndex);
            interactiveGeometryEnabled_ =
                pipelineScenario_ == PipelineComparisonScenario::FullD3D12;
            drawingTimerMode_ = DrawingTimerMode::Qpc;
        }
        else
        {
            drawingTimerMode_ = timerComparisonCaseIndex_ % 2 == 0
                ? DrawingTimerMode::WindowTimer
                : DrawingTimerMode::Qpc;
        }
        circleMode_ = true;
        SendMessageW(Control(kLineRadioId), BM_SETCHECK, BST_UNCHECKED, 0);
        SendMessageW(Control(kCircleRadioId), BM_SETCHECK, BST_CHECKED, 0);
        SetWindowTextW(
            Control(kIntervalEditId), std::to_wstring(drawIntervalMs_).c_str());
        timerComparisonIntervalsTicks_.clear();
        timerComparisonAddedCount_ = 0;
        timerComparisonLastAdditionTicks_ = 0;
        qpcSkippedDeadlines_ = 0;
        timerComparisonPhase_ = TimerComparisonPhase::Warmup;
        if ((!pipelineComparisonActive_ ||
             pipelineScenario_ != PipelineComparisonScenario::RendererIdle) &&
            !StartRandomDrawing())
        {
            throw std::runtime_error(
                "Could not start timer comparison warm-up");
        }
        LARGE_INTEGER now{};
        if (!QueryPerformanceCounter(&now))
        {
            throw std::runtime_error(
                "QueryPerformanceCounter failed at timer warm-up start");
        }
        timerComparisonPhaseDeadline_.QuadPart = now.QuadPart +
            static_cast<LONGLONG>(
                timerComparisonFrequency_.QuadPart *
                kTimerComparisonWarmupMs / 1000);
        std::cout << (pipelineComparisonActive_ ? "[pipeline-test] warm-up " : "[timer-test] warm-up ")
                  << (pipelineComparisonActive_ ? PipelineScenarioName(pipelineScenario_) : TimerModeName(drawingTimerMode_))
                  << " target=" << drawIntervalMs_ << " ms for "
                  << (pipelineComparisonActive_ ? kPipelineComparisonWarmupMs : kTimerComparisonWarmupMs)
                  << " ms\n";
        if (pipelineComparisonActive_)
        {
            LARGE_INTEGER pipelineNow{};
            if (!QueryPerformanceCounter(&pipelineNow))
            {
                throw std::runtime_error("QueryPerformanceCounter failed at pipeline warm-up start");
            }
            timerComparisonPhaseDeadline_.QuadPart = pipelineNow.QuadPart +
                static_cast<LONGLONG>(timerComparisonFrequency_.QuadPart *
                    kPipelineComparisonWarmupMs / 1000);
        }
    }

    void PrintTimerComparisonResult()
    {
        const LONGLONG measurementTicks =
            timerComparisonMeasurementEnd_.QuadPart -
            timerComparisonMeasurementStart_.QuadPart;
        const double durationSeconds =
            static_cast<double>(measurementTicks) /
            static_cast<double>(timerComparisonFrequency_.QuadPart);
        const double objectsPerSecond = durationSeconds > 0.0
            ? static_cast<double>(timerComparisonAddedCount_) / durationSeconds
            : 0.0;

        std::vector<LONGLONG> sortedIntervals =
            timerComparisonIntervalsTicks_;
        std::sort(sortedIntervals.begin(), sortedIntervals.end());
        double meanMs = 0.0;
        double medianMs = 0.0;
        double p95Ms = 0.0;
        if (!sortedIntervals.empty())
        {
            LONGLONG sumTicks = 0;
            for (const LONGLONG ticks : sortedIntervals)
            {
                sumTicks += ticks;
            }
            const auto ticksToMs = [this](LONGLONG ticks) {
                return static_cast<double>(ticks) * 1000.0 /
                    static_cast<double>(timerComparisonFrequency_.QuadPart);
            };
            meanMs = ticksToMs(sumTicks) /
                static_cast<double>(sortedIntervals.size());
            const std::size_t middle = sortedIntervals.size() / 2;
            medianMs = sortedIntervals.size() % 2 == 0
                ? ticksToMs(sortedIntervals[middle - 1] +
                            sortedIntervals[middle]) * 0.5
                : ticksToMs(sortedIntervals[middle]);
            const std::size_t p95Index = std::min(
                sortedIntervals.size() - 1,
                static_cast<std::size_t>(std::ceil(
                    0.95 * static_cast<double>(sortedIntervals.size()))) - 1);
            p95Ms = ticksToMs(sortedIntervals[p95Index]);
        }

        std::cout << std::fixed << std::setprecision(2)
                  << "[timer-test] result mode="
                  << TimerModeName(drawingTimerMode_)
                  << " target_ms=" << drawIntervalMs_
                  << " measured_ms=" << durationSeconds * 1000.0
                  << " objects=" << timerComparisonAddedCount_
                  << " objects_per_s=" << objectsPerSecond;
        if (sortedIntervals.empty())
        {
            std::cout << " interval_mean_ms=n/a interval_median_ms=n/a"
                         " interval_p95_ms=n/a";
        }
        else
        {
            std::cout << " interval_mean_ms=" << meanMs
                      << " interval_median_ms=" << medianMs
                      << " interval_p95_ms=" << p95Ms;
        }
        if (drawingTimerMode_ == DrawingTimerMode::Qpc)
        {
            std::cout << " skipped_qpc_deadlines=" << qpcSkippedDeadlines_;
        }
        std::cout << '\n' << std::flush;
    }

    void GenerateVisualCaptureScene()
    {
        circleParameters_ = {
            {0.00f, 0.00f, 4.0f, 1.0f, 1.0f, 0.1f, 0.1f, 24},
            {1.00f, 0.00f, 8.0f, 3.0f, 0.1f, 1.0f, 0.1f, 24},
            {0.00f, 1.00f, 16.0f, 6.0f, 0.1f, 0.3f, 1.0f, 34},
            {1.00f, 1.00f, 28.0f, 10.0f, 1.0f, 0.8f, 0.1f, 59},
            {0.12f, 0.18f, 9.0f, 2.0f, 1.0f, 0.1f, 0.9f, 24},
            {0.15f, 0.20f, 20.0f, 5.0f, 0.9f, 0.1f, 0.9f, 42},
            {0.18f, 0.22f, 36.0f, 12.0f, 0.2f, 0.9f, 0.9f, 76},
            {0.50f, 0.50f, 2.0f, 3.0f, 1.0f, 1.0f, 1.0f, 24},
            {0.50f, 0.50f, 70.0f, 4.0f, 0.1f, 0.1f, 0.1f, 128},
            {0.52f, 0.50f, 70.0f, 16.0f, 1.0f, 0.2f, 0.1f, 128},
            {0.50f, 0.53f, 110.0f, 8.0f, 0.2f, 1.0f, 0.2f, 128},
        };
        requestedCircleCount_ = static_cast<UINT>(circleParameters_.size());
    }

    void CreateVisualReadback()
    {
        D3D12_RESOURCE_DESC source{}; source.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        source.Width = width_; source.Height = height_; source.DepthOrArraySize = 1;
        source.MipLevels = 1; source.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        source.SampleDesc.Count = 1; source.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        device_->GetCopyableFootprints(&source, 0, 1, 0, &visualReadbackFootprint_, nullptr, nullptr, &visualReadbackSize_);
        D3D12_HEAP_PROPERTIES heap{}; heap.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC buffer{}; buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        buffer.Width = visualReadbackSize_; buffer.Height = 1; buffer.DepthOrArraySize = 1;
        buffer.MipLevels = 1; buffer.SampleDesc.Count = 1; buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        ThrowIfFailed(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&visualReadback_)), "CreateCommittedResource(visual readback)");
    }

    void SaveVisualReadback(const std::wstring& path)
    {
        void* mapped = nullptr; D3D12_RANGE range{0, visualReadbackSize_};
        ThrowIfFailed(visualReadback_->Map(0, &range, &mapped), "Map(visual readback)");
        std::ofstream output(path, std::ios::binary);
        if (!output) { visualReadback_->Unmap(0, nullptr); throw std::runtime_error("Could not open visual capture output"); }
        const UINT rowBytes = width_ * 4; const UINT fileSize = 54 + rowBytes * height_;
        std::array<unsigned char, 54> header{}; header[0] = 'B'; header[1] = 'M';
        const UINT pixelOffset = 54, dibSize = 40; const LONG width = static_cast<LONG>(width_);
        const LONG negativeHeight = -static_cast<LONG>(height_); const unsigned short planes = 1, bits = 32;
        std::memcpy(header.data() + 2, &fileSize, 4); std::memcpy(header.data() + 10, &pixelOffset, 4);
        std::memcpy(header.data() + 14, &dibSize, 4); std::memcpy(header.data() + 18, &width, 4);
        std::memcpy(header.data() + 22, &negativeHeight, 4); std::memcpy(header.data() + 26, &planes, 2);
        std::memcpy(header.data() + 28, &bits, 2); output.write(reinterpret_cast<const char*>(header.data()), header.size());
        const auto* source = static_cast<const unsigned char*>(mapped);
        for (UINT y = 0; y < height_; ++y) for (UINT x = 0; x < width_; ++x)
        {
            const auto* pixel = source + y * visualReadbackFootprint_.Footprint.RowPitch + x * 4;
            const unsigned char bgra[4] = {pixel[2], pixel[1], pixel[0], pixel[3]};
            output.write(reinterpret_cast<const char*>(bgra), 4);
        }
        visualReadback_->Unmap(0, nullptr);
    }

    void AddCircleThroughputElapsed(double& totalMilliseconds, UINT64 ticks) const
    {
        if (circleThroughputQpcFrequency_.QuadPart <= 0)
        {
            throw std::runtime_error(
                "Circle-throughput QPC frequency is unavailable");
        }
        totalMilliseconds += static_cast<double>(ticks) * 1000.0 /
            static_cast<double>(circleThroughputQpcFrequency_.QuadPart);
    }

    void OnInteractiveShapeAdded()
    {
        if (!drawingShapes_.empty())
        {
            OnInteractiveShapesAdded(drawingShapes_.size() - 1, 1);
        }
    }

    void OnInteractiveShapesAdded(
        std::size_t firstShapeIndex,
        UINT shapeCount)
    {
        if (shapeCount == 0)
        {
            return;
        }
        objectCount_ = drawingShapes_.size();
        objectMemoryBytes_ = drawingShapes_.capacity() * sizeof(DrawingShape);
        if (rendererMode_ == RendererMode::Direct2D)
        {
            canvasDirty_ = true;
        }
        else if (interactiveGeometryEnabled_)
        {
            canvasDirty_ = true;
            LARGE_INTEGER geometryStart{};
            LARGE_INTEGER uploadStart{};
            LARGE_INTEGER uploadStop{};
            const bool measureStages = circleThroughputBenchmarkActive_ ||
                (pipelineComparisonActive_ &&
                 timerComparisonPhase_ == TimerComparisonPhase::Measuring);
            if (measureStages && !QueryPerformanceCounter(&geometryStart))
            {
                throw std::runtime_error("QueryPerformanceCounter failed before geometry preparation");
            }
            for (UINT index = 0; index < shapeCount; ++index)
            {
                AppendInteractiveShapeGeometry(
                    drawingShapes_[firstShapeIndex + index]);
            }
            if (measureStages && !QueryPerformanceCounter(&uploadStart))
            {
                throw std::runtime_error("QueryPerformanceCounter failed after geometry preparation");
            }
            UploadInteractiveGeometryTail();
            UploadShaderInstanceTail();
            if (measureStages && !QueryPerformanceCounter(&uploadStop))
            {
                throw std::runtime_error("QueryPerformanceCounter failed after geometry upload");
            }
            CollectRetiredGeometryBuffers();
            if (measureStages)
            {
                const UINT64 geometryTicks = static_cast<UINT64>(
                    uploadStart.QuadPart - geometryStart.QuadPart);
                const UINT64 uploadTicks = static_cast<UINT64>(
                    uploadStop.QuadPart - uploadStart.QuadPart);
                if (pipelineComparisonActive_ &&
                    timerComparisonPhase_ == TimerComparisonPhase::Measuring)
                {
                    pipelineGeometryPrepTicks_ += geometryTicks;
                    pipelineGeometryUploadTicks_ += uploadTicks;
                }
                if (circleThroughputBenchmarkActive_)
                {
                    AddCircleThroughputElapsed(
                        circleThroughputMetrics_.rendererGeometryPreparationMilliseconds,
                        geometryTicks);
                    AddCircleThroughputElapsed(
                        circleThroughputMetrics_.rendererGeometryUploadMilliseconds,
                        uploadTicks);
                }
            }
        }
        if (!randomDrawing_)
        {
            UpdateInformationPanel();
        }
    }

    bool GenerateRandomShape()
    {
        if (canvasWidth_ == 0 || height_ == 0)
        {
            return false;
        }
        std::uniform_int_distribution<int> thicknessRange(1, 20);
        std::uniform_int_distribution<int> colorRange(0, 255);
        const bool circle = circleMode_;
        int x1 = 0;
        int y1 = 0;
        int x2 = 0;
        int y2 = 0;
        if (circle)
        {
            const int maxRadius = std::min(
                static_cast<int>(std::min(canvasWidth_, height_)) / 2, 100);
            if (maxRadius <= 0)
            {
                return false;
            }
            std::uniform_int_distribution<int> radiusRange(1, maxRadius);
            const int radius = radiusRange(randomGenerator_);
            std::uniform_int_distribution<int> centerXRange(
                radius, static_cast<int>(canvasWidth_) - radius);
            std::uniform_int_distribution<int> centerYRange(
                radius, static_cast<int>(height_) - radius);
            const int centerX = centerXRange(randomGenerator_);
            const int centerY = centerYRange(randomGenerator_);
            x1 = centerX - radius;
            y1 = centerY - radius;
            x2 = centerX + radius;
            y2 = centerY + radius;
        }
        else
        {
            std::uniform_int_distribution<int> xRange(
                0, static_cast<int>(canvasWidth_) - 1);
            std::uniform_int_distribution<int> yRange(
                0, static_cast<int>(height_) - 1);
            x1 = xRange(randomGenerator_);
            y1 = yRange(randomGenerator_);
            x2 = xRange(randomGenerator_);
            y2 = yRange(randomGenerator_);
        }
        const COLORREF color = RGB(
            colorRange(randomGenerator_),
            colorRange(randomGenerator_),
            colorRange(randomGenerator_));
        drawingShapes_.push_back({
            x1, y1, x2, y2,
            thicknessRange(randomGenerator_), color, circle});
        return true;
    }

    void RebuildInteractiveGeometry()
    {
        if (!interactiveLayout_ || requestedCircleCount_ != 0 || !device_ ||
            !interactiveGeometryEnabled_)
        {
            return;
        }
        canvasRenderedShapeCount_ = 0;
        if (commandQueue_ && fence_ && fenceEvent_ != nullptr)
        {
            WaitForGpu();
        }
        vertices_.clear();
        indices_.clear();
        circleDraws_.clear();
        shaderCircleInstances_.clear();
        shapeCircleInstanceIndices_.clear();
        for (const DrawingShape& shape : drawingShapes_)
        {
            AppendInteractiveShapeGeometry(shape);
        }
        if (vertices_.empty() && shaderCircleInstances_.empty())
        {
            vertexBuffer_.Reset();
            indexBuffer_.Reset();
            shaderInstanceBuffer_.Reset();
            vertexBufferView_ = {};
            indexBufferView_ = {};
            shaderInstanceBufferView_ = {};
            vertexBufferCapacityBytes_ = 0;
            indexBufferCapacityBytes_ = 0;
            shaderInstanceBufferCapacityBytes_ = 0;
            lastUploadedVertexCount_ = 0;
            lastUploadedIndexCount_ = 0;
            lastUploadedShaderInstanceCount_ = 0;
            return;
        }
        if (vertices_.empty())
        {
            vertexBuffer_.Reset();
            indexBuffer_.Reset();
            vertexBufferView_ = {};
            indexBufferView_ = {};
            vertexBufferCapacityBytes_ = 0;
            indexBufferCapacityBytes_ = 0;
            lastUploadedVertexCount_ = 0;
            lastUploadedIndexCount_ = 0;
        }
        else
        {
            UploadCircleBuffers();
        }
        UploadShaderInstanceBuffer();
    }

    void AppendInteractiveShapeGeometry(const DrawingShape& shape)
    {
        if (rendererMode_ == RendererMode::Shader && shape.circle)
        {
            const float centerX = (static_cast<float>(shape.x1) +
                                   static_cast<float>(shape.x2)) * 0.5f;
            const float centerY = (static_cast<float>(shape.y1) +
                                   static_cast<float>(shape.y2)) * 0.5f;
            const float radius = std::max(
                1.0f, (static_cast<float>(shape.x2 - shape.x1)) * 0.5f);
            const UINT instanceIndex = static_cast<UINT>(shaderCircleInstances_.size());
            shaderCircleInstances_.push_back({
                canvasWidth_ == 0 ? 0.0f : centerX / static_cast<float>(canvasWidth_),
                height_ == 0 ? 0.0f : centerY / static_cast<float>(height_),
                radius,
                static_cast<float>(shape.thickness),
                static_cast<float>(GetRValue(shape.color)) / 255.0f,
                static_cast<float>(GetGValue(shape.color)) / 255.0f,
                static_cast<float>(GetBValue(shape.color)) / 255.0f,
                1.0f});
            shapeCircleInstanceIndices_.push_back(instanceIndex);
            circleDraws_.push_back({
                0, 0, 0,
                canvasWidth_ == 0 ? 0.0f : centerX / static_cast<float>(canvasWidth_),
                height_ == 0 ? 0.0f : centerY / static_cast<float>(height_),
                radius});
            return;
        }

        shapeCircleInstanceIndices_.push_back(std::numeric_limits<UINT>::max());
        constexpr float twoPi = 6.28318530717958647692f;
        const std::size_t baseVertex = vertices_.size();
        const std::size_t startIndex = indices_.size();
        float centerX = 0.0f;
        float centerY = 0.0f;
        UINT segments = 1;
        if (shape.circle)
        {
            centerX = (static_cast<float>(shape.x1) +
                       static_cast<float>(shape.x2)) * 0.5f;
            centerY = (static_cast<float>(shape.y1) +
                       static_cast<float>(shape.y2)) * 0.5f;
            const float radius = std::max(
                1.0f, (static_cast<float>(shape.x2 - shape.x1)) * 0.5f);
            segments = std::clamp(
                static_cast<UINT>(twoPi * radius / 3.0f),
                kMinimumSegments, kMaximumSegments);
            const float innerRadius = std::max(
                0.0f, radius - static_cast<float>(shape.thickness));
            const float red = static_cast<float>(GetRValue(shape.color)) / 255.0f;
            const float green = static_cast<float>(GetGValue(shape.color)) / 255.0f;
            const float blue = static_cast<float>(GetBValue(shape.color)) / 255.0f;
            for (UINT point = 0; point <= segments; ++point)
            {
                const float angle = twoPi * static_cast<float>(point) /
                                    static_cast<float>(segments);
                const float cosine = std::cos(angle);
                const float sine = std::sin(angle);
                vertices_.push_back({cosine * radius, sine * radius,
                                     red, green, blue, 1.0f});
                vertices_.push_back({cosine * innerRadius,
                                     sine * innerRadius,
                                     red, green, blue, 1.0f});
            }
            for (UINT segment = 0; segment < segments; ++segment)
            {
                const UINT outer = segment * 2;
                const UINT nextOuter = outer + 2;
                const UINT inner = outer + 1;
                const UINT nextInner = nextOuter + 1;
                indices_.insert(indices_.end(), {
                    static_cast<std::uint16_t>(outer),
                    static_cast<std::uint16_t>(nextOuter),
                    static_cast<std::uint16_t>(nextInner),
                    static_cast<std::uint16_t>(outer),
                    static_cast<std::uint16_t>(nextInner),
                    static_cast<std::uint16_t>(inner)});
            }
        }
        else
        {
            const float dx = static_cast<float>(shape.x2 - shape.x1);
            const float dy = static_cast<float>(shape.y2 - shape.y1);
            const float length = std::max(1.0f, std::sqrt(dx * dx + dy * dy));
            const float halfWidth = static_cast<float>(shape.thickness) * 0.5f;
            const float ox = -dy * halfWidth / length;
            const float oy = dx * halfWidth / length;
            const float red = static_cast<float>(GetRValue(shape.color)) / 255.0f;
            const float green = static_cast<float>(GetGValue(shape.color)) / 255.0f;
            const float blue = static_cast<float>(GetBValue(shape.color)) / 255.0f;
            vertices_.push_back({static_cast<float>(shape.x1) + ox,
                                 static_cast<float>(shape.y1) + oy,
                                 red, green, blue, 1.0f});
            vertices_.push_back({static_cast<float>(shape.x1) - ox,
                                 static_cast<float>(shape.y1) - oy,
                                 red, green, blue, 1.0f});
            vertices_.push_back({static_cast<float>(shape.x2) + ox,
                                 static_cast<float>(shape.y2) + oy,
                                 red, green, blue, 1.0f});
            vertices_.push_back({static_cast<float>(shape.x2) - ox,
                                 static_cast<float>(shape.y2) - oy,
                                 red, green, blue, 1.0f});
            indices_.insert(indices_.end(), {0, 2, 3, 0, 3, 1});
        }
        circleDraws_.push_back({
            shape.circle ? segments * 6U : 6U,
            static_cast<UINT>(startIndex),
            static_cast<INT>(baseVertex),
            canvasWidth_ == 0 ? 0.0f : centerX / static_cast<float>(canvasWidth_),
            height_ == 0 ? 0.0f : centerY / static_cast<float>(height_),
            shape.circle ? static_cast<float>(shape.x2 - shape.x1) * 0.5f : 0.0f});
    }

    void UploadInteractiveGeometryTail()
    {
        if (vertices_.empty() && indices_.empty())
        {
            lastUploadedVertexCount_ = 0;
            lastUploadedIndexCount_ = 0;
            return;
        }
        if (vertices_.size() >
                std::numeric_limits<UINT>::max() / sizeof(Vertex) ||
            indices_.size() >
                std::numeric_limits<UINT>::max() / sizeof(std::uint16_t))
        {
            throw std::overflow_error(
                "Interactive geometry exceeds D3D12 buffer limits");
        }
        const UINT vertexBytes = static_cast<UINT>(vertices_.size() * sizeof(Vertex));
        const UINT indexBytes = static_cast<UINT>(indices_.size() * sizeof(std::uint16_t));
        if (vertexBytes > vertexBufferCapacityBytes_ ||
            indexBytes > indexBufferCapacityBytes_)
        {
            const auto nextCapacity = [](UINT current, UINT required) {
                UINT capacity = std::max(256U, current);
                while (capacity < required)
                {
                    if (capacity > std::numeric_limits<UINT>::max() / 2)
                    {
                        return required;
                    }
                    capacity *= 2;
                }
                return capacity;
            };
            const UINT newVertexCapacity = nextCapacity(
                vertexBufferCapacityBytes_, vertexBytes);
            const UINT newIndexCapacity = nextCapacity(
                indexBufferCapacityBytes_, indexBytes);
            ComPtr<ID3D12Resource> newVertexBuffer;
            ComPtr<ID3D12Resource> newIndexBuffer;
            CreateUploadBuffer(vertices_.data(), vertexBytes,
                               newVertexCapacity, &newVertexBuffer,
                               "interactive vertex buffer");
            CreateUploadBuffer(indices_.data(), indexBytes,
                               newIndexCapacity, &newIndexBuffer,
                               "interactive index buffer");
            RetireCurrentGeometryBuffers();
            vertexBuffer_ = std::move(newVertexBuffer);
            indexBuffer_ = std::move(newIndexBuffer);
            vertexBufferCapacityBytes_ = newVertexCapacity;
            indexBufferCapacityBytes_ = newIndexCapacity;
        }
        else
        {
            const std::size_t vertexCount = vertices_.size();
            const std::size_t indexCount = indices_.size();
            if (vertexCount > lastUploadedVertexCount_)
            {
                void* mapped = nullptr;
                D3D12_RANGE readRange{0, 0};
                ThrowIfFailed(vertexBuffer_->Map(0, &readRange, &mapped),
                              "Map(interactive vertex buffer)");
                std::memcpy(static_cast<std::byte*>(mapped) +
                                lastUploadedVertexCount_ * sizeof(Vertex),
                            vertices_.data() + lastUploadedVertexCount_,
                            (vertexCount - lastUploadedVertexCount_) * sizeof(Vertex));
                vertexBuffer_->Unmap(0, nullptr);
            }
            if (indexCount > lastUploadedIndexCount_)
            {
                void* mapped = nullptr;
                D3D12_RANGE readRange{0, 0};
                ThrowIfFailed(indexBuffer_->Map(0, &readRange, &mapped),
                              "Map(interactive index buffer)");
                std::memcpy(static_cast<std::byte*>(mapped) +
                                lastUploadedIndexCount_ * sizeof(std::uint16_t),
                            indices_.data() + lastUploadedIndexCount_,
                            (indexCount - lastUploadedIndexCount_) * sizeof(std::uint16_t));
                indexBuffer_->Unmap(0, nullptr);
            }
        }
        lastUploadedVertexCount_ = vertices_.size();
        lastUploadedIndexCount_ = indices_.size();
        vertexBufferView_.BufferLocation = vertexBuffer_->GetGPUVirtualAddress();
        vertexBufferView_.SizeInBytes = vertexBytes;
        vertexBufferView_.StrideInBytes = sizeof(Vertex);
        indexBufferView_.BufferLocation = indexBuffer_->GetGPUVirtualAddress();
        indexBufferView_.SizeInBytes = indexBytes;
        indexBufferView_.Format = DXGI_FORMAT_R16_UINT;
    }

    void RetireCurrentGeometryBuffers()
    {
        if (vertexBuffer_ || indexBuffer_)
        {
            retiredGeometryBuffers_.push_back({
                nextFenceValue_ - 1,
                std::move(vertexBuffer_),
                std::move(indexBuffer_),
                {}});
        }
    }

    void CollectRetiredGeometryBuffers()
    {
        if (!fence_)
        {
            return;
        }
        const UINT64 completedValue = fence_->GetCompletedValue();
        retiredGeometryBuffers_.erase(
            std::remove_if(
                retiredGeometryBuffers_.begin(), retiredGeometryBuffers_.end(),
                [completedValue](const RetiredGeometryBuffers& buffers) {
                    return buffers.fenceValue <= completedValue;
                }),
            retiredGeometryBuffers_.end());
    }

    HWND Control(int id) const
    {
        const std::size_t index =
            static_cast<std::size_t>(id - kThicknessSliderId + 1);
        return index < controls_.size() ? controls_[index] : nullptr;
    }

    void PositionControl(int id, int x, int y, int width, int height)
    {
        HWND control = Control(id);
        if (control == nullptr)
        {
            return;
        }
        if (!SetWindowPos(
                control,
                nullptr,
                x,
                y,
                std::max(0, width),
                std::max(0, height),
                SWP_NOACTIVATE | SWP_NOZORDER))
        {
            ReportLayoutFailure(L"SetWindowPos(panel control)");
        }
    }

    void ReportLayoutFailure(const wchar_t* operation)
    {
        std::wstring message = operation;
        message += L" selhalo; aplikace bude ukoncena.";
        MessageBoxW(
            mainWindow_, message.c_str(), L"Chyba rozvrzeni",
            MB_OK | MB_ICONERROR);
        PostMessageW(mainWindow_, WM_CLOSE, 0, 0);
    }

    bool ValidateIntervalText()
    {
        wchar_t text[8]{};
        GetWindowTextW(Control(kIntervalEditId), text, _countof(text));
        wchar_t* end = nullptr;
        const unsigned long value = wcstoul(text, &end, 10);
        if (diagnosticsEnabled_)
        {
            std::cout << "[diag] interval edit parsed value_ms=" << value
                      << '\n' << std::flush;
        }
        if (text[0] == L'\0' || end == text || *end != L'\0' || value > 1000)
        {
            MessageBoxW(
                mainWindow_,
                L"Interval musi byt cele cislo od 0 do 1000 ms.",
                L"Neplatny interval",
                MB_OK | MB_ICONWARNING);
            SetWindowTextW(Control(kIntervalEditId), L"100");
            drawIntervalMs_ = 100;
            return false;
        }
        drawIntervalMs_ = static_cast<UINT>(value);
        return true;
    }

    void ToggleRandomDrawing()
    {
        if (randomDrawing_)
        {
            StopRandomDrawing();
            return;
        }
        if (!ValidateIntervalText())
        {
            return;
        }
        StartRandomDrawing();
    }

    bool StartRandomDrawing()
    {
        randomDrawing_ = true;
        generatedInSpeedWindow_ = 0;
        renderedInSpeedWindow_ = 0;
        pendingGeneratedShapesForPresent_ = 0;
        generatedShapeCountThisRun_ = 0;
        slowWindowCount_ = 0;
        fastWindowCount_ = 0;
        batchingEnabled_ = drawIntervalMs_ == 0;
        speedWindowStartMs_ = GetTickCount64();
        drawingRunStartMs_ = speedWindowStartMs_;
        actualObjectsPerSecond_ = 0;
        SetWindowTextW(Control(kStartButtonId), L"Stop");
        EnableWindow(Control(kIntervalEditId), FALSE);
        EnableWindow(Control(kLineRadioId), FALSE);
        EnableWindow(Control(kCircleRadioId), FALSE);
        if (drawingTimerMode_ == DrawingTimerMode::Qpc)
        {
            if (drawingQpcFrequency_.QuadPart <= 0 &&
                !QueryPerformanceFrequency(&drawingQpcFrequency_))
            {
                throw std::runtime_error(
                    "QueryPerformanceFrequency failed for QPC drawing mode");
            }
            drawingIntervalTicks_ = std::max<LONGLONG>(
                1,
                drawingQpcFrequency_.QuadPart * drawIntervalMs_ / 1000);
            if (!QueryPerformanceCounter(&drawingStartQpc_))
            {
                throw std::runtime_error(
                    "QueryPerformanceCounter failed when starting QPC drawing");
            }
            nextDrawingDeadline_ = drawingStartQpc_;
            nextDrawingDeadline_.QuadPart += drawingIntervalTicks_;
        }
        else
        {
            const UINT timerPeriod = std::max(1U, drawIntervalMs_);
            if (diagnosticsEnabled_)
            {
                std::cout << "[diag] SetTimer requested interval_ms="
                          << timerPeriod << '\n' << std::flush;
            }
            if (SetTimer(mainWindow_, kDrawingTimerId, timerPeriod, nullptr) == 0)
            {
                randomDrawing_ = false;
                EnableWindow(Control(kIntervalEditId), TRUE);
                EnableWindow(Control(kLineRadioId), TRUE);
                EnableWindow(Control(kCircleRadioId), TRUE);
                MessageBoxW(
                    mainWindow_, L"Nelze spustit casovac kresleni.",
                    L"Chyba casovace", MB_OK | MB_ICONERROR);
                return false;
            }
        }
        SetWindowTextW(Control(kHintLabelId), L"Nahodne kresleni je aktivni.");
        return true;
    }

    void StopRandomDrawing()
    {
        randomDrawing_ = false;
        KillTimer(mainWindow_, kDrawingTimerId);
        SetWindowTextW(Control(kStartButtonId), L"Start nahodneho kresleni");
        EnableWindow(Control(kIntervalEditId), TRUE);
        EnableWindow(Control(kLineRadioId), TRUE);
        EnableWindow(Control(kCircleRadioId), TRUE);
        SetWindowTextW(
            Control(kHintLabelId),
            L"SDL kruznice jsou aproximovane useckami.");
    }

    void ShowColorDialog()
    {
        CHOOSECOLORW dialog{};
        dialog.lStructSize = sizeof(dialog);
        dialog.hwndOwner = mainWindow_;
        dialog.rgbResult = selectedColor_;
        dialog.lpCustColors = customColors_.data();
        dialog.Flags = CC_FULLOPEN | CC_RGBINIT;
        if (ChooseColorW(&dialog))
        {
            selectedColor_ = dialog.rgbResult;
            wchar_t caption[64]{};
            swprintf_s(
                caption,
                L"Select color (#%02X%02X%02X)",
                GetRValue(selectedColor_),
                GetGValue(selectedColor_),
                GetBValue(selectedColor_));
            SetWindowTextW(Control(kColorButtonId), caption);
        }
        else if (CommDlgExtendedError() != 0)
        {
            MessageBoxW(
                mainWindow_,
                L"Paletu barev se nepodarilo otevrit.",
                L"Chyba vyberu barvy",
                MB_OK | MB_ICONERROR);
        }
    }

    void UpdateInformationPanel()
    {
        if (!interactiveLayout_ || Control(kObjectCountLabelId) == nullptr)
        {
            return;
        }

        wchar_t text[128]{};
        swprintf_s(text, L"Pocet objektu: %zu", objectCount_);
        SetWindowTextW(Control(kObjectCountLabelId), text);
        swprintf_s(
            text,
            L"Odhad dat objektu (ne proces): %zu B",
            objectMemoryBytes_);
        SetWindowTextW(Control(kObjectMemoryLabelId), text);
        swprintf_s(text, L"Rychlost: %u obj/s", actualObjectsPerSecond_);
        SetWindowTextW(Control(kSpeedLabelId), text);

        PROCESS_MEMORY_COUNTERS_EX memory{};
        memory.cb = sizeof(memory);
        if (GetProcessMemoryInfo(
                GetCurrentProcess(),
                reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),
                sizeof(memory)))
        {
            if (privateMemoryBaseline_ == 0)
            {
                privateMemoryBaseline_ = memory.PrivateUsage;
            }
            swprintf_s(
                text,
                L"Soukr. pamet: %zu KB",
                memory.PrivateUsage / 1024);
            SetWindowTextW(Control(kPrivateMemoryLabelId), text);
            swprintf_s(
                text,
                L"Zaklad: %zu KB",
                privateMemoryBaseline_ / 1024);
            SetWindowTextW(Control(kMemoryBaselineLabelId), text);
            const LONGLONG delta =
                static_cast<LONGLONG>(memory.PrivateUsage) -
                static_cast<LONGLONG>(privateMemoryBaseline_);
            swprintf_s(
                text,
                L"Zmena od startu: %lld KB",
                delta / 1024);
            SetWindowTextW(Control(kMemoryDeltaLabelId), text);
        }
    }

    void UpdateCanvasWidth()
    {
        canvasWidth_ = width_;
    }

    void RecordAndSubmitFrame(UINT frameIndex)
    {
        lastFrameDrawCallCount_ = 0;
        lastFrameRenderedCircleCount_ = 0;
        lastRendererFrameSample_ = {};
        const bool measureRendererStages =
            rendererBenchmarkActive_ || circleThroughputBenchmarkActive_;
        LARGE_INTEGER recordStart{};
        ULONGLONG recordCycleStart = 0;
        if (measureRendererStages &&
            (!QueryPerformanceCounter(&recordStart) ||
             !QueryThreadCycleTime(GetCurrentThread(), &recordCycleStart)))
        {
            throw std::runtime_error("Could not start renderer command timing");
        }
        ThrowIfFailed(
            commandAllocators_[frameIndex]->Reset(),
            "ID3D12CommandAllocator::Reset");
        ThrowIfFailed(
            commandList_->Reset(commandAllocators_[frameIndex].Get(), nullptr),
            "ID3D12GraphicsCommandList::Reset");

        D3D12_RESOURCE_BARRIER toRenderTarget{};
        toRenderTarget.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        toRenderTarget.Transition.pResource = renderTargets_[frameIndex].Get();
        toRenderTarget.Transition.Subresource =
            D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        toRenderTarget.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        toRenderTarget.Transition.StateAfter =
            interactiveLayout_ ? D3D12_RESOURCE_STATE_COPY_DEST
                               : D3D12_RESOURCE_STATE_RENDER_TARGET;
        commandList_->ResourceBarrier(1, &toRenderTarget);

        D3D12_CPU_DESCRIPTOR_HANDLE rtv =
            renderTargetHeap_->GetCPUDescriptorHandleForHeapStart();
        rtv.ptr += static_cast<SIZE_T>(frameIndex) * rtvDescriptorSize_;
        const float whiteColor[] = {1.0f, 1.0f, 1.0f, 1.0f};
        if (interactiveLayout_)
        {
            if (canvasNeedsClear_)
            {
                commandList_->ClearRenderTargetView(
                    persistentCanvasRtv_, whiteColor, 0, nullptr);
            }
            if (canvasDirty_)
            {
                commandList_->OMSetRenderTargets(
                    1, &persistentCanvasRtv_, FALSE, nullptr);
            }
        }
        else
        {
            commandList_->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
            const float clearColor[] = {0.08f, 0.30f, 0.72f, 1.0f};
            commandList_->ClearRenderTargetView(rtv, clearColor, 0, nullptr);
        }

        D3D12_VIEWPORT viewport{};
        viewport.TopLeftX = 0.0f;
        viewport.TopLeftY = 0.0f;
        viewport.Width = static_cast<float>(width_);
        viewport.Height = static_cast<float>(height_);
        viewport.MinDepth = 0.0f;
        viewport.MaxDepth = 1.0f;
        commandList_->RSSetViewports(1, &viewport);

        D3D12_RECT scissor{};
        scissor.left = 0;
        scissor.top = 0;
        scissor.right = static_cast<LONG>(width_);
        scissor.bottom = static_cast<LONG>(height_);
        commandList_->RSSetScissorRects(1, &scissor);

        if (!interactiveLayout_ || canvasDirty_)
        {
            if (gpuTimestampEnabled_)
            {
                commandList_->EndQuery(
                    gpuTimestampQueryHeap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
                    frameIndex * 2);
            }
            if (rendererMode_ == RendererMode::Legacy)
            {
                commandList_->SetGraphicsRootSignature(rootSignature_.Get());
                commandList_->SetPipelineState(pipelineState_.Get());
                commandList_->IASetPrimitiveTopology(
                    D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            }
            if (!vertices_.empty())
            {
                commandList_->IASetVertexBuffers(0, 1, &vertexBufferView_);
                commandList_->IASetIndexBuffer(&indexBufferView_);
            }
            if (rendererMode_ == RendererMode::Shader &&
                !shaderCircleInstances_.empty())
            {
                commandList_->IASetVertexBuffers(
                    1, 1, &shaderInstanceBufferView_);
            }
            if (canvasRenderedShapeCount_ > circleDraws_.size())
            {
                throw std::runtime_error(
                    "Persistent canvas shape count exceeds available geometry");
            }
            std::size_t drawIndex =
                canvasNeedsClear_ ? 0 : canvasRenderedShapeCount_;
            while (drawIndex < circleDraws_.size())
            {
                const CircleDraw& circle = circleDraws_[drawIndex];
                const bool shaderCircle =
                    rendererMode_ == RendererMode::Shader &&
                    circle.indexCount == 0;
                if (shaderCircle)
                {
                    std::size_t endIndex = drawIndex + 1;
                    while (endIndex < circleDraws_.size() &&
                           circleDraws_[endIndex].indexCount == 0)
                    {
                        ++endIndex;
                    }
                    commandList_->SetGraphicsRootSignature(
                        shaderRootSignature_.Get());
                    commandList_->SetPipelineState(shaderPipelineState_.Get());
                    commandList_->IASetPrimitiveTopology(
                        D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
                    const float viewportSize[] = {
                        static_cast<float>(width_),
                        static_cast<float>(height_)};
                    commandList_->SetGraphicsRoot32BitConstants(
                        0, static_cast<UINT>(std::size(viewportSize)),
                        viewportSize, 0);
                    commandList_->DrawInstanced(
                        4,
                        static_cast<UINT>(endIndex - drawIndex),
                        0,
                        shapeCircleInstanceIndices_[drawIndex]);
                    ++lastFrameDrawCallCount_;
                    lastFrameRenderedCircleCount_ +=
                        static_cast<UINT>(endIndex - drawIndex);
                    drawIndex = endIndex;
                    continue;
                }

                if (circle.indexCount != 0)
                {
                    if (canvasWidth_ == 0)
                    {
                        break;
                    }
                    if (rendererMode_ == RendererMode::Shader)
                    {
                        commandList_->SetGraphicsRootSignature(rootSignature_.Get());
                        commandList_->SetPipelineState(pipelineState_.Get());
                        commandList_->IASetPrimitiveTopology(
                            D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                    }
                    const float canvasWidth = static_cast<float>(canvasWidth_);
                    const float centerX = std::clamp(
                        circle.normalizedCenterX * canvasWidth,
                        circle.outerRadius,
                        std::max(circle.outerRadius,
                                 canvasWidth - circle.outerRadius));
                    const float centerY = std::clamp(
                        circle.normalizedCenterY * static_cast<float>(height_),
                        circle.outerRadius,
                        std::max(circle.outerRadius,
                                 static_cast<float>(height_) - circle.outerRadius));
                    const float rootConstants[] = {
                        static_cast<float>(width_),
                        static_cast<float>(height_),
                        centerX,
                        centerY,
                    };
                    commandList_->SetGraphicsRoot32BitConstants(
                        0, static_cast<UINT>(std::size(rootConstants)),
                        rootConstants, 0);
                    commandList_->DrawIndexedInstanced(
                        circle.indexCount, 1, circle.startIndexLocation,
                        circle.baseVertexLocation, 0);
                    ++lastFrameDrawCallCount_;
                    if (circle.outerRadius > 0.0f)
                    {
                        ++lastFrameRenderedCircleCount_;
                    }
                }
                ++drawIndex;
            }
            if (gpuTimestampEnabled_)
            {
                commandList_->EndQuery(
                    gpuTimestampQueryHeap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
                    frameIndex * 2 + 1);
                commandList_->ResolveQueryData(
                    gpuTimestampQueryHeap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
                    frameIndex * 2, 2, gpuTimestampReadback_.Get(),
                    static_cast<UINT64>(frameIndex) * 2 * sizeof(UINT64));
                gpuTimestampPending_[frameIndex] = true;
                if (rendererBenchmarkGpuSampling_)
                {
                    ++rendererBenchmarkGpuQueriesSubmitted_;
                }
            }
        }

        if (interactiveLayout_)
        {
            D3D12_RESOURCE_BARRIER canvasToCopy{};
            canvasToCopy.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            canvasToCopy.Transition.pResource = persistentCanvas_.Get();
            canvasToCopy.Transition.Subresource =
                D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            canvasToCopy.Transition.StateBefore =
                D3D12_RESOURCE_STATE_RENDER_TARGET;
            canvasToCopy.Transition.StateAfter =
                D3D12_RESOURCE_STATE_COPY_SOURCE;
            commandList_->ResourceBarrier(1, &canvasToCopy);
            commandList_->CopyResource(
                renderTargets_[frameIndex].Get(), persistentCanvas_.Get());
            canvasToCopy.Transition.StateBefore =
                D3D12_RESOURCE_STATE_COPY_SOURCE;
            canvasToCopy.Transition.StateAfter =
                D3D12_RESOURCE_STATE_RENDER_TARGET;
            commandList_->ResourceBarrier(1, &canvasToCopy);
            canvasNeedsClear_ = false;
            canvasDirty_ = false;
            canvasRenderedShapeCount_ = circleDraws_.size();
        }
        if (visualCaptureEnabled_)
        {
            D3D12_RESOURCE_BARRIER captureBarrier{};
            captureBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            captureBarrier.Transition.pResource = renderTargets_[frameIndex].Get();
            captureBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            captureBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
            captureBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
            commandList_->ResourceBarrier(1, &captureBarrier);
            D3D12_TEXTURE_COPY_LOCATION source{};
            source.pResource = renderTargets_[frameIndex].Get();
            source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            source.SubresourceIndex = 0;
            D3D12_TEXTURE_COPY_LOCATION destination{};
            destination.pResource = visualReadback_.Get();
            destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            destination.PlacedFootprint = visualReadbackFootprint_;
            commandList_->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
            captureBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
            captureBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
            commandList_->ResourceBarrier(1, &captureBarrier);
        }
        toRenderTarget.Transition.StateBefore =
            interactiveLayout_ ? D3D12_RESOURCE_STATE_COPY_DEST
                               : D3D12_RESOURCE_STATE_RENDER_TARGET;
        toRenderTarget.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
        commandList_->ResourceBarrier(1, &toRenderTarget);
        ThrowIfFailed(commandList_->Close(), "ID3D12GraphicsCommandList::Close");
        LARGE_INTEGER recordStop{};
        ULONGLONG recordCycleStop = 0;
        if (measureRendererStages &&
            (!QueryPerformanceCounter(&recordStop) ||
             !QueryThreadCycleTime(GetCurrentThread(), &recordCycleStop)))
        {
            throw std::runtime_error("Could not stop renderer command recording timing");
        }

        ID3D12CommandList* commandLists[] = {commandList_.Get()};
        commandQueue_->ExecuteCommandLists(1, commandLists);
        LARGE_INTEGER executeStop{};
        ULONGLONG executeCycleStop = 0;
        if (measureRendererStages &&
            (!QueryPerformanceCounter(&executeStop) ||
             !QueryThreadCycleTime(GetCurrentThread(), &executeCycleStop)))
        {
            throw std::runtime_error("Could not stop renderer command execution timing");
        }
        if (measureRendererStages)
        {
            lastRendererFrameSample_.recordCycles =
                recordCycleStop - recordCycleStart;
            lastRendererFrameSample_.executeCycles =
                executeCycleStop - recordCycleStop;
            lastRendererFrameSample_.recordElapsedTicks = static_cast<UINT64>(
                recordStop.QuadPart - recordStart.QuadPart);
            lastRendererFrameSample_.executeElapsedTicks = static_cast<UINT64>(
                executeStop.QuadPart - recordStop.QuadPart);
            rendererBenchmarkRecordElapsedTicks_ += static_cast<UINT64>(
                recordStop.QuadPart - recordStart.QuadPart);
            rendererBenchmarkExecuteElapsedTicks_ += static_cast<UINT64>(
                executeStop.QuadPart - recordStop.QuadPart);
            rendererBenchmarkRecordCycles_ += recordCycleStop - recordCycleStart;
            rendererBenchmarkExecuteCycles_ += executeCycleStop - recordCycleStop;
            if (circleThroughputBenchmarkActive_)
            {
                AddCircleThroughputElapsed(
                    circleThroughputMetrics_.rendererCommandRecordMilliseconds,
                    static_cast<UINT64>(
                        recordStop.QuadPart - recordStart.QuadPart));
                AddCircleThroughputElapsed(
                    circleThroughputMetrics_.rendererCommandExecuteMilliseconds,
                    static_cast<UINT64>(
                        executeStop.QuadPart - recordStop.QuadPart));
            }
        }
    }

    void SignalFrame(UINT frameIndex)
    {
        const UINT64 fenceValue = nextFenceValue_++;
        ThrowIfFailed(
            commandQueue_->Signal(fence_.Get(), fenceValue),
            "ID3D12CommandQueue::Signal");
        frameFenceValues_[frameIndex] = fenceValue;
    }

    void EnableDebugLayerIfAvailable()
    {
        ComPtr<ID3D12Debug> debugController;
        const HRESULT result =
            D3D12GetDebugInterface(IID_PPV_ARGS(&debugController));
        if (SUCCEEDED(result))
        {
            debugController->EnableDebugLayer();
            debugLayerEnabled_ = true;
            std::cout << "D3D12 debug layer: enabled\n";
        }
        else
        {
            std::cout << "D3D12 debug layer: unavailable (HRESULT=0x"
                      << std::hex << std::uppercase << std::setw(8)
                      << std::setfill('0')
                      << static_cast<unsigned long>(result)
                      << std::dec << std::nouppercase << ")\n";
        }
    }

    void CheckDebugMessages()
    {
        if (!debugLayerEnabled_)
        {
            return;
        }

        ComPtr<ID3D12InfoQueue> infoQueue;
        ThrowIfFailed(
            device_.As(&infoQueue),
            "ID3D12Device::QueryInterface(ID3D12InfoQueue)");
        const UINT64 messageCount = infoQueue->GetNumStoredMessages();
        UINT errorCount = 0;
        UINT warningCount = 0;
        for (UINT64 index = 0; index < messageCount; ++index)
        {
            SIZE_T messageSize = 0;
            ThrowIfFailed(
                infoQueue->GetMessage(index, nullptr, &messageSize),
                "ID3D12InfoQueue::GetMessage(size)");
            std::vector<char> storage(messageSize);
            auto* message =
                reinterpret_cast<D3D12_MESSAGE*>(storage.data());
            ThrowIfFailed(
                infoQueue->GetMessage(index, message, &messageSize),
                "ID3D12InfoQueue::GetMessage");
            if (message->Severity == D3D12_MESSAGE_SEVERITY_CORRUPTION ||
                message->Severity == D3D12_MESSAGE_SEVERITY_ERROR)
            {
                ++errorCount;
                std::cout << "D3D12 error: " << message->pDescription << '\n';
            }
            else if (message->Severity == D3D12_MESSAGE_SEVERITY_WARNING)
            {
                ++warningCount;
                std::cout << "D3D12 warning: "
                          << message->pDescription << '\n';
            }
        }
        std::cout << "D3D12 debug messages: " << errorCount
                  << " error(s), " << warningCount << " warning(s)\n";
        if (errorCount != 0)
        {
            throw std::runtime_error(
                "D3D12 debug layer reported errors; see benchmark output");
        }
    }

    void CreateCirclePipeline()
    {
        static constexpr char shaderSource[] = R"(
cbuffer ViewConstants : register(b0)
{
    float2 viewportSize;
    float2 circleCenter;
};

struct VSInput
{
    float2 position : POSITION;
    float4 color : COLOR;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
};

PSInput VSMain(VSInput input)
{
    PSInput output;
    float2 pixelPosition = circleCenter + input.position;
    float2 clipPosition = float2(
        pixelPosition.x * 2.0f / viewportSize.x - 1.0f,
        1.0f - pixelPosition.y * 2.0f / viewportSize.y);
    output.position = float4(clipPosition, 0.0f, 1.0f);
    output.color = input.color;
    return output;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    return input.color;
}
)";

        ComPtr<ID3DBlob> vertexShader;
        ComPtr<ID3DBlob> pixelShader;
        ComPtr<ID3DBlob> diagnostics;
        HRESULT result = D3DCompile(
            shaderSource,
            sizeof(shaderSource) - 1,
            "native_circle.hlsl",
            nullptr,
            nullptr,
            "VSMain",
            "vs_5_0",
            D3DCOMPILE_ENABLE_STRICTNESS,
            0,
            &vertexShader,
            &diagnostics);
        if (FAILED(result))
        {
            ThrowShaderCompileFailure(result, "D3DCompile(VSMain)", diagnostics);
        }

        diagnostics.Reset();
        result = D3DCompile(
            shaderSource,
            sizeof(shaderSource) - 1,
            "native_circle.hlsl",
            nullptr,
            nullptr,
            "PSMain",
            "ps_5_0",
            D3DCOMPILE_ENABLE_STRICTNESS,
            0,
            &pixelShader,
            &diagnostics);
        if (FAILED(result))
        {
            ThrowShaderCompileFailure(result, "D3DCompile(PSMain)", diagnostics);
        }

        D3D12_ROOT_PARAMETER rootParameter{};
        rootParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        rootParameter.Constants.ShaderRegister = 0;
        rootParameter.Constants.RegisterSpace = 0;
        rootParameter.Constants.Num32BitValues = 4;
        rootParameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

        D3D12_ROOT_SIGNATURE_DESC rootDescription{};
        rootDescription.NumParameters = 1;
        rootDescription.pParameters = &rootParameter;
        rootDescription.NumStaticSamplers = 0;
        rootDescription.pStaticSamplers = nullptr;
        rootDescription.Flags =
            D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

        ComPtr<ID3DBlob> serializedRootSignature;
        diagnostics.Reset();
        result = D3D12SerializeRootSignature(
            &rootDescription,
            D3D_ROOT_SIGNATURE_VERSION_1,
            &serializedRootSignature,
            &diagnostics);
        if (FAILED(result))
        {
            ThrowShaderCompileFailure(
                result, "D3D12SerializeRootSignature", diagnostics);
        }
        ThrowIfFailed(
            device_->CreateRootSignature(
                0,
                serializedRootSignature->GetBufferPointer(),
                serializedRootSignature->GetBufferSize(),
                IID_PPV_ARGS(&rootSignature_)),
            "ID3D12Device::CreateRootSignature");

        const D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0,
             static_cast<UINT>(offsetof(Vertex, x)),
             D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0,
             static_cast<UINT>(offsetof(Vertex, red)),
             D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        };

        D3D12_GRAPHICS_PIPELINE_STATE_DESC pipelineDescription{};
        pipelineDescription.pRootSignature = rootSignature_.Get();
        pipelineDescription.VS = {
            vertexShader->GetBufferPointer(), vertexShader->GetBufferSize()};
        pipelineDescription.PS = {
            pixelShader->GetBufferPointer(), pixelShader->GetBufferSize()};
        pipelineDescription.BlendState.AlphaToCoverageEnable = FALSE;
        pipelineDescription.BlendState.IndependentBlendEnable = FALSE;
        auto& renderTargetBlend =
            pipelineDescription.BlendState.RenderTarget[0];
        renderTargetBlend.BlendEnable = FALSE;
        renderTargetBlend.LogicOpEnable = FALSE;
        renderTargetBlend.SrcBlend = D3D12_BLEND_ONE;
        renderTargetBlend.DestBlend = D3D12_BLEND_ZERO;
        renderTargetBlend.BlendOp = D3D12_BLEND_OP_ADD;
        renderTargetBlend.SrcBlendAlpha = D3D12_BLEND_ONE;
        renderTargetBlend.DestBlendAlpha = D3D12_BLEND_ZERO;
        renderTargetBlend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        renderTargetBlend.LogicOp = D3D12_LOGIC_OP_NOOP;
        renderTargetBlend.RenderTargetWriteMask =
            D3D12_COLOR_WRITE_ENABLE_ALL;
        pipelineDescription.SampleMask = UINT_MAX;
        auto& rasterizer = pipelineDescription.RasterizerState;
        rasterizer.FillMode = D3D12_FILL_MODE_SOLID;
        rasterizer.CullMode = D3D12_CULL_MODE_NONE;
        rasterizer.FrontCounterClockwise = FALSE;
        rasterizer.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
        rasterizer.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
        rasterizer.SlopeScaledDepthBias =
            D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
        rasterizer.DepthClipEnable = TRUE;
        rasterizer.MultisampleEnable = FALSE;
        rasterizer.AntialiasedLineEnable = FALSE;
        rasterizer.ForcedSampleCount = 0;
        rasterizer.ConservativeRaster =
            D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
        pipelineDescription.DepthStencilState.DepthEnable = FALSE;
        pipelineDescription.DepthStencilState.DepthWriteMask =
            D3D12_DEPTH_WRITE_MASK_ZERO;
        pipelineDescription.DepthStencilState.DepthFunc =
            D3D12_COMPARISON_FUNC_ALWAYS;
        pipelineDescription.DepthStencilState.StencilEnable = FALSE;
        pipelineDescription.DepthStencilState.StencilReadMask =
            D3D12_DEFAULT_STENCIL_READ_MASK;
        pipelineDescription.DepthStencilState.StencilWriteMask =
            D3D12_DEFAULT_STENCIL_WRITE_MASK;
        pipelineDescription.InputLayout = {inputLayout, _countof(inputLayout)};
        pipelineDescription.PrimitiveTopologyType =
            D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pipelineDescription.NumRenderTargets = 1;
        pipelineDescription.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        pipelineDescription.SampleDesc.Count = 1;
        pipelineDescription.SampleDesc.Quality = 0;

        ThrowIfFailed(
            device_->CreateGraphicsPipelineState(
                &pipelineDescription, IID_PPV_ARGS(&pipelineState_)),
            "ID3D12Device::CreateGraphicsPipelineState");

    }

    void CreateProceduralCirclePipeline()
    {
        static constexpr char shaderSource[] = R"(
cbuffer ViewConstants : register(b0)
{
    float2 viewportSize;
};

struct VSInput
{
    float2 center : CENTER;
    float radius : RADIUS;
    float thickness : THICKNESS;
    float4 color : COLOR;
    uint vertexId : SV_VertexID;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float2 localPosition : TEXCOORD0;
    float radius : TEXCOORD1;
    float thickness : TEXCOORD2;
    float4 color : COLOR;
};

PSInput VSMain(VSInput input)
{
    static const float2 corners[4] = {
        float2(-1.0f, -1.0f), float2(1.0f, -1.0f),
        float2(-1.0f, 1.0f), float2(1.0f, 1.0f)
    };
    PSInput output;
    float2 center = float2(
        clamp(input.center.x * viewportSize.x, input.radius,
              max(input.radius, viewportSize.x - input.radius)),
        clamp(input.center.y * viewportSize.y, input.radius,
              max(input.radius, viewportSize.y - input.radius)));
    float2 localPosition = corners[input.vertexId & 3] * (input.radius + 1.5f);
    float2 pixelPosition = center + localPosition;
    float2 clipPosition = float2(
        pixelPosition.x * 2.0f / viewportSize.x - 1.0f,
        1.0f - pixelPosition.y * 2.0f / viewportSize.y);
    output.position = float4(clipPosition, 0.0f, 1.0f);
    output.localPosition = localPosition;
    output.radius = input.radius;
    output.thickness = input.thickness;
    output.color = input.color;
    return output;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    float distanceFromCenter = length(input.localPosition);
    float antialiasWidth = max(fwidth(distanceFromCenter), 0.75f);
    float outerCoverage = 1.0f - smoothstep(
        input.radius - antialiasWidth,
        input.radius + antialiasWidth,
        distanceFromCenter);
    float innerRadius = max(0.0f, input.radius - input.thickness);
    float innerCoverage = innerRadius <= 0.0f
        ? 1.0f
        : smoothstep(
            innerRadius - antialiasWidth,
            innerRadius + antialiasWidth,
            distanceFromCenter);
    float coverage = outerCoverage * innerCoverage;
    return float4(input.color.rgb, input.color.a * coverage);
}
)";

        ComPtr<ID3DBlob> vertexShader;
        ComPtr<ID3DBlob> pixelShader;
        ComPtr<ID3DBlob> diagnostics;
        HRESULT result = D3DCompile(
            shaderSource, sizeof(shaderSource) - 1, "procedural_circle.hlsl",
            nullptr, nullptr, "VSMain", "vs_5_0",
            D3DCOMPILE_ENABLE_STRICTNESS, 0, &vertexShader, &diagnostics);
        if (FAILED(result))
        {
            ThrowShaderCompileFailure(result, "D3DCompile(procedural VS)", diagnostics);
        }
        diagnostics.Reset();
        result = D3DCompile(
            shaderSource, sizeof(shaderSource) - 1, "procedural_circle.hlsl",
            nullptr, nullptr, "PSMain", "ps_5_0",
            D3DCOMPILE_ENABLE_STRICTNESS, 0, &pixelShader, &diagnostics);
        if (FAILED(result))
        {
            ThrowShaderCompileFailure(result, "D3DCompile(procedural PS)", diagnostics);
        }

        D3D12_ROOT_PARAMETER parameter{};
        parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        parameter.Constants.ShaderRegister = 0;
        parameter.Constants.Num32BitValues = 2;
        parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
        D3D12_ROOT_SIGNATURE_DESC rootDescription{};
        rootDescription.NumParameters = 1;
        rootDescription.pParameters = &parameter;
        rootDescription.Flags =
            D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
        ComPtr<ID3DBlob> serializedRootSignature;
        diagnostics.Reset();
        result = D3D12SerializeRootSignature(
            &rootDescription, D3D_ROOT_SIGNATURE_VERSION_1,
            &serializedRootSignature, &diagnostics);
        if (FAILED(result))
        {
            ThrowShaderCompileFailure(
                result, "D3D12SerializeRootSignature(procedural)", diagnostics);
        }
        ThrowIfFailed(
            device_->CreateRootSignature(
                0, serializedRootSignature->GetBufferPointer(),
                serializedRootSignature->GetBufferSize(),
                IID_PPV_ARGS(&shaderRootSignature_)),
            "ID3D12Device::CreateRootSignature(procedural)");

        const D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
            {"CENTER", 0, DXGI_FORMAT_R32G32_FLOAT, 1, 0,
             D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
            {"RADIUS", 0, DXGI_FORMAT_R32_FLOAT, 1, 8,
             D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
            {"THICKNESS", 0, DXGI_FORMAT_R32_FLOAT, 1, 12,
             D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
            {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 16,
             D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1},
        };
        D3D12_GRAPHICS_PIPELINE_STATE_DESC description{};
        description.pRootSignature = shaderRootSignature_.Get();
        description.VS = {vertexShader->GetBufferPointer(), vertexShader->GetBufferSize()};
        description.PS = {pixelShader->GetBufferPointer(), pixelShader->GetBufferSize()};
        auto& blend = description.BlendState.RenderTarget[0];
        blend.BlendEnable = TRUE;
        blend.LogicOpEnable = FALSE;
        blend.SrcBlend = D3D12_BLEND_SRC_ALPHA;
        blend.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
        blend.BlendOp = D3D12_BLEND_OP_ADD;
        blend.SrcBlendAlpha = D3D12_BLEND_ONE;
        blend.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
        blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        blend.LogicOp = D3D12_LOGIC_OP_NOOP;
        blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        description.SampleMask = UINT_MAX;
        description.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        description.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        description.RasterizerState.DepthClipEnable = TRUE;
        description.RasterizerState.MultisampleEnable = FALSE;
        description.RasterizerState.AntialiasedLineEnable = FALSE;
        description.RasterizerState.ForcedSampleCount = 0;
        description.RasterizerState.ConservativeRaster =
            D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
        description.DepthStencilState.DepthEnable = FALSE;
        description.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
        description.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        description.DepthStencilState.StencilEnable = FALSE;
        description.InputLayout = {inputLayout, _countof(inputLayout)};
        description.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        description.NumRenderTargets = 1;
        description.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        description.SampleDesc.Count = 1;
        ThrowIfFailed(
            device_->CreateGraphicsPipelineState(
                &description, IID_PPV_ARGS(&shaderPipelineState_)),
            "ID3D12Device::CreateGraphicsPipelineState(procedural)");
    }

    static void ThrowShaderCompileFailure(
        HRESULT result,
        const char* operation,
        const ComPtr<ID3DBlob>& diagnostics)
    {
        std::string message;
        if (diagnostics && diagnostics->GetBufferPointer() != nullptr)
        {
            message.assign(
                static_cast<const char*>(diagnostics->GetBufferPointer()),
                diagnostics->GetBufferSize());
        }

        try
        {
            ThrowIfFailed(result, operation);
        }
        catch (const std::exception& error)
        {
            if (!message.empty())
            {
                throw std::runtime_error(
                    std::string(error.what()) + "\n" + message);
            }
            throw;
        }
    }

    void GenerateCircleParameters()
    {
        circleParameters_.clear();
        circleParameters_.reserve(requestedCircleCount_);

        if (requestedCircleCount_ == kSingleCircleCount)
        {
            circleParameters_.push_back({
                0.5f,
                0.5f,
                kOuterRadius,
                kOuterRadius - kInnerRadius,
                1.0f,
                0.88f,
                0.12f,
                kCircleSegments,
            });
            return;
        }
        if (requestedCircleCount_ == 0)
        {
            return;
        }

        DeterministicRng random(kSeed);
        const float cellWidth =
            static_cast<float>(canvasWidth_) /
            static_cast<float>(kGridColumns);
        const float cellHeight =
            static_cast<float>(height_) / static_cast<float>(kGridRows);
        constexpr float twoPi = 6.28318530717958647692f;

        for (UINT index = 0; index < requestedCircleCount_; ++index)
        {
            const float outerRadius =
                static_cast<float>(random.InRange(15, 22));
            const float thickness =
                static_cast<float>(random.InRange(4, 8));
            float centerX = 0.0f;
            float centerY = 0.0f;
            if (requestedCircleCount_ == kManyCircleCount)
            {
                const UINT column = index % kGridColumns;
                const UINT row = index / kGridColumns;
                centerX =
                    (static_cast<float>(column) + 0.5f) * cellWidth +
                    static_cast<float>(random.InRangeSigned(-4, 4));
                centerY =
                    (static_cast<float>(row) + 0.5f) * cellHeight +
                    static_cast<float>(random.InRangeSigned(-6, 6));
            }
            else
            {
                const INT radius = static_cast<INT>(outerRadius);
                centerX = static_cast<float>(random.InRangeSigned(
                    radius, static_cast<INT>(canvasWidth_) - radius));
                centerY = static_cast<float>(random.InRangeSigned(
                    radius, static_cast<INT>(height_) - radius));
            }
            const UINT red = random.InRange(96, 255);
            const UINT green = random.InRange(96, 255);
            const UINT blue = random.InRange(96, 255);
            const UINT segments = std::clamp(
                static_cast<UINT>(
                    twoPi * outerRadius / 3.0f),
                kMinimumSegments,
                kMaximumSegments);

            circleParameters_.push_back({
                centerX / static_cast<float>(canvasWidth_),
                centerY / static_cast<float>(height_),
                outerRadius,
                thickness,
                static_cast<float>(red) / 255.0f,
                static_cast<float>(green) / 255.0f,
                static_cast<float>(blue) / 255.0f,
                segments,
            });
        }
    }

    void CreateCircleThroughputDrawingShapes()
    {
        circleThroughputShapes_.clear();
        circleThroughputShapes_.reserve(circleParameters_.size());
        for (const CircleParameters& circle : circleParameters_)
        {
            const int centerX = static_cast<int>(std::lround(
                circle.normalizedCenterX * static_cast<float>(canvasWidth_)));
            const int centerY = static_cast<int>(std::lround(
                circle.normalizedCenterY * static_cast<float>(height_)));
            const int radius = static_cast<int>(std::lround(circle.outerRadius));
            const int thickness = static_cast<int>(std::lround(circle.thickness));
            const COLORREF color = RGB(
                static_cast<BYTE>(std::lround(circle.red * 255.0f)),
                static_cast<BYTE>(std::lround(circle.green * 255.0f)),
                static_cast<BYTE>(std::lround(circle.blue * 255.0f)));
            circleThroughputShapes_.push_back({
                centerX - radius,
                centerY - radius,
                centerX + radius,
                centerY + radius,
                thickness,
                color,
                true});
        }
        if (circleThroughputShapes_.size() != circleThroughputTargetCount_)
        {
            throw std::runtime_error(
                "Prepared throughput scene does not match the requested circle count");
        }
    }

    void GenerateCircleGeometry()
    {
        vertices_.clear();
        indices_.clear();
        circleDraws_.clear();
        shapeCircleInstanceIndices_.clear();
        shaderCircleInstances_.clear();
        vertices_.reserve(circleParameters_.size() * (kMaximumSegments + 1) * 2);
        indices_.reserve(circleParameters_.size() * kMaximumSegments * 6);
        circleDraws_.reserve(circleParameters_.size());

        constexpr float twoPi = 6.28318530717958647692f;
        for (const CircleParameters& circle : circleParameters_)
        {
            const UINT circleVertexCount = (circle.segments + 1) * 2;
            const UINT circleIndexCount = circle.segments * 6;
            const std::size_t baseVertex = vertices_.size();
            const std::size_t startIndex = indices_.size();
            if (circleVertexCount - 1 >
                    std::numeric_limits<std::uint16_t>::max() ||
                baseVertex >
                    static_cast<std::size_t>(
                        std::numeric_limits<INT>::max()) ||
                startIndex >
                    static_cast<std::size_t>(
                        std::numeric_limits<UINT>::max()))
            {
                throw std::overflow_error(
                    "Circle geometry exceeds the selected index ranges");
            }

            const float innerRadius =
                std::max(0.0f, circle.outerRadius - circle.thickness);
            for (UINT point = 0; point <= circle.segments; ++point)
            {
                const float angle =
                    twoPi * static_cast<float>(point) /
                    static_cast<float>(circle.segments);
                const float cosine = std::cos(angle);
                const float sine = std::sin(angle);
                vertices_.push_back({
                    cosine * circle.outerRadius,
                    sine * circle.outerRadius,
                    circle.red,
                    circle.green,
                    circle.blue,
                    1.0f,
                });
                vertices_.push_back({
                    cosine * innerRadius,
                    sine * innerRadius,
                    circle.red,
                    circle.green,
                    circle.blue,
                    1.0f,
                });
            }

            for (UINT segment = 0; segment < circle.segments; ++segment)
            {
                const UINT outer = segment * 2;
                const UINT nextOuter = outer + 2;
                const UINT inner = outer + 1;
                const UINT nextInner = nextOuter + 1;
                indices_.push_back(static_cast<std::uint16_t>(outer));
                indices_.push_back(static_cast<std::uint16_t>(nextOuter));
                indices_.push_back(static_cast<std::uint16_t>(nextInner));
                indices_.push_back(static_cast<std::uint16_t>(outer));
                indices_.push_back(static_cast<std::uint16_t>(nextInner));
                indices_.push_back(static_cast<std::uint16_t>(inner));
            }

            circleDraws_.push_back({
                circleIndexCount,
                static_cast<UINT>(startIndex),
                static_cast<INT>(baseVertex),
                circle.normalizedCenterX,
                circle.normalizedCenterY,
                circle.outerRadius,
            });
            shapeCircleInstanceIndices_.push_back(
                std::numeric_limits<UINT>::max());
        }

        if (circleDraws_.size() != requestedCircleCount_)
        {
            throw std::runtime_error(
                "Generated circle count does not match requested count");
        }
    }

    void GenerateShaderCircleInstances()
    {
        vertices_.clear();
        indices_.clear();
        circleDraws_.clear();
        shaderCircleInstances_.clear();
        shapeCircleInstanceIndices_.clear();
        shaderCircleInstances_.reserve(circleParameters_.size());
        circleDraws_.reserve(circleParameters_.size());
        shapeCircleInstanceIndices_.reserve(circleParameters_.size());
        for (const CircleParameters& circle : circleParameters_)
        {
            const UINT instanceIndex = static_cast<UINT>(shaderCircleInstances_.size());
            shaderCircleInstances_.push_back({
                circle.normalizedCenterX,
                circle.normalizedCenterY,
                circle.outerRadius,
                circle.thickness,
                circle.red,
                circle.green,
                circle.blue,
                1.0f});
            shapeCircleInstanceIndices_.push_back(instanceIndex);
            circleDraws_.push_back({
                0, 0, 0,
                circle.normalizedCenterX,
                circle.normalizedCenterY,
                circle.outerRadius});
        }
        if (circleDraws_.size() != requestedCircleCount_)
        {
            throw std::runtime_error(
                "Generated shader circle count does not match requested count");
        }
    }

    void UploadCircleBuffers()
    {
        if (vertices_.empty() || indices_.empty())
        {
            if (requestedCircleCount_ == 0)
            {
                return;
            }
            throw std::runtime_error("Cannot upload empty circle geometry");
        }
        if (vertices_.size() >
                std::numeric_limits<UINT>::max() / sizeof(Vertex) ||
            indices_.size() >
                std::numeric_limits<UINT>::max() /
                    sizeof(std::uint16_t))
        {
            throw std::overflow_error("Circle buffers exceed D3D12 view sizes");
        }

        const UINT vertexBufferSize =
            static_cast<UINT>(vertices_.size() * sizeof(Vertex));
        CreateUploadBuffer(
            vertices_.data(),
            vertexBufferSize,
            &vertexBuffer_,
            "vertex buffer");
        vertexBufferView_.BufferLocation =
            vertexBuffer_->GetGPUVirtualAddress();
        vertexBufferView_.SizeInBytes = vertexBufferSize;
        vertexBufferView_.StrideInBytes = sizeof(Vertex);

        const UINT indexBufferSize =
            static_cast<UINT>(indices_.size() * sizeof(std::uint16_t));
        CreateUploadBuffer(
            indices_.data(),
            indexBufferSize,
            &indexBuffer_,
            "index buffer");
        indexBufferView_.BufferLocation =
            indexBuffer_->GetGPUVirtualAddress();
        indexBufferView_.SizeInBytes = indexBufferSize;
        indexBufferView_.Format = DXGI_FORMAT_R16_UINT;
        vertexBufferCapacityBytes_ = vertexBufferSize;
        indexBufferCapacityBytes_ = indexBufferSize;
        lastUploadedVertexCount_ = vertices_.size();
        lastUploadedIndexCount_ = indices_.size();
    }

    static UINT NextBufferCapacity(UINT current, UINT required)
    {
        UINT capacity = std::max(256U, current);
        while (capacity < required)
        {
            if (capacity > std::numeric_limits<UINT>::max() / 2)
            {
                return required;
            }
            capacity *= 2;
        }
        return capacity;
    }

    void UploadShaderInstanceBuffer()
    {
        if (shaderCircleInstances_.empty())
        {
            shaderInstanceBuffer_.Reset();
            shaderInstanceBufferView_ = {};
            shaderInstanceBufferCapacityBytes_ = 0;
            lastUploadedShaderInstanceCount_ = 0;
            return;
        }
        if (shaderCircleInstances_.size() >
            std::numeric_limits<UINT>::max() / sizeof(CircleInstance))
        {
            throw std::overflow_error("Shader instance buffer exceeds D3D12 view sizes");
        }
        const UINT byteSize = static_cast<UINT>(
            shaderCircleInstances_.size() * sizeof(CircleInstance));
        const UINT capacity = NextBufferCapacity(0, byteSize);
        CreateUploadBuffer(
            shaderCircleInstances_.data(), byteSize, capacity,
            &shaderInstanceBuffer_, "shader circle instance buffer");
        shaderInstanceBufferCapacityBytes_ = capacity;
        lastUploadedShaderInstanceCount_ = shaderCircleInstances_.size();
        shaderInstanceBufferView_.BufferLocation =
            shaderInstanceBuffer_->GetGPUVirtualAddress();
        shaderInstanceBufferView_.SizeInBytes = capacity;
        shaderInstanceBufferView_.StrideInBytes = sizeof(CircleInstance);
    }

    void RetireShaderInstanceBuffer()
    {
        if (shaderInstanceBuffer_)
        {
            retiredGeometryBuffers_.push_back({
                nextFenceValue_ - 1, {}, {}, std::move(shaderInstanceBuffer_)});
        }
    }

    void UploadShaderInstanceTail()
    {
        if (shaderCircleInstances_.empty())
        {
            return;
        }
        if (shaderCircleInstances_.size() >
            std::numeric_limits<UINT>::max() / sizeof(CircleInstance))
        {
            throw std::overflow_error("Shader instance buffer exceeds D3D12 view sizes");
        }
        const UINT requiredBytes = static_cast<UINT>(
            shaderCircleInstances_.size() * sizeof(CircleInstance));
        if (requiredBytes > shaderInstanceBufferCapacityBytes_)
        {
            const UINT capacity = NextBufferCapacity(
                shaderInstanceBufferCapacityBytes_, requiredBytes);
            ComPtr<ID3D12Resource> newBuffer;
            CreateUploadBuffer(
                shaderCircleInstances_.data(), requiredBytes, capacity,
                &newBuffer, "shader circle instance buffer growth");
            RetireShaderInstanceBuffer();
            shaderInstanceBuffer_ = std::move(newBuffer);
            shaderInstanceBufferCapacityBytes_ = capacity;
        }
        else if (shaderCircleInstances_.size() > lastUploadedShaderInstanceCount_)
        {
            void* mapped = nullptr;
            D3D12_RANGE readRange{0, 0};
            ThrowIfFailed(
                shaderInstanceBuffer_->Map(0, &readRange, &mapped),
                "Map(shader circle instance buffer)");
            const std::size_t offset =
                lastUploadedShaderInstanceCount_ * sizeof(CircleInstance);
            const std::size_t bytes =
                (shaderCircleInstances_.size() - lastUploadedShaderInstanceCount_) *
                sizeof(CircleInstance);
            std::memcpy(
                static_cast<std::byte*>(mapped) + offset,
                shaderCircleInstances_.data() + lastUploadedShaderInstanceCount_,
                bytes);
            shaderInstanceBuffer_->Unmap(0, nullptr);
        }
        lastUploadedShaderInstanceCount_ = shaderCircleInstances_.size();
        shaderInstanceBufferView_.BufferLocation =
            shaderInstanceBuffer_->GetGPUVirtualAddress();
        shaderInstanceBufferView_.SizeInBytes = shaderInstanceBufferCapacityBytes_;
        shaderInstanceBufferView_.StrideInBytes = sizeof(CircleInstance);
    }

    void CreateUploadBuffer(
        const void* source,
        UINT byteSize,
        ComPtr<ID3D12Resource>* resource,
        const char* description)
    {
        CreateUploadBuffer(source, byteSize, byteSize, resource, description);
    }

    void CreateUploadBuffer(
        const void* source,
        UINT byteSize,
        UINT capacityBytes,
        ComPtr<ID3D12Resource>* resource,
        const char* description)
    {
        D3D12_HEAP_PROPERTIES heapProperties{};
        heapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;
        heapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        heapProperties.CreationNodeMask = 1;
        heapProperties.VisibleNodeMask = 1;

        D3D12_RESOURCE_DESC resourceDescription{};
        resourceDescription.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        resourceDescription.Alignment = 0;
        resourceDescription.Width = capacityBytes;
        resourceDescription.Height = 1;
        resourceDescription.DepthOrArraySize = 1;
        resourceDescription.MipLevels = 1;
        resourceDescription.Format = DXGI_FORMAT_UNKNOWN;
        resourceDescription.SampleDesc.Count = 1;
        resourceDescription.SampleDesc.Quality = 0;
        resourceDescription.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        resourceDescription.Flags = D3D12_RESOURCE_FLAG_NONE;

        ThrowIfFailed(
            device_->CreateCommittedResource(
                &heapProperties,
                D3D12_HEAP_FLAG_NONE,
                &resourceDescription,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr,
                IID_PPV_ARGS(resource->ReleaseAndGetAddressOf())),
            description);

        void* mapped = nullptr;
        D3D12_RANGE readRange{0, 0};
        ThrowIfFailed(
            (*resource)->Map(0, &readRange, &mapped),
            description);
        std::memcpy(mapped, source, byteSize);
        (*resource)->Unmap(0, nullptr);
    }

    void CreateDeviceAndQueue()
    {
        if (benchmarkEnabled_)
        {
            EnableDebugLayerIfAvailable();
        }
        ThrowIfFailed(
            CreateDXGIFactory2(0, IID_PPV_ARGS(&factory_)),
            "CreateDXGIFactory2");
        CheckTearingSupport();

        ComPtr<IDXGIAdapter1> selectedAdapter;
        DXGI_ADAPTER_DESC1 selectedDescription{};
        HRESULT lastDeviceResult = DXGI_ERROR_NOT_FOUND;
        if (gpuSelection_ == GpuSelection::Auto)
        {
            bool foundHardwareAdapter = false;
            for (UINT adapterIndex = 0;; ++adapterIndex)
            {
                ComPtr<IDXGIAdapter1> adapter;
                const HRESULT enumResult =
                    factory_->EnumAdapters1(adapterIndex, &adapter);
                if (enumResult == DXGI_ERROR_NOT_FOUND)
                {
                    break;
                }
                ThrowIfFailed(enumResult, "IDXGIFactory::EnumAdapters1");

                DXGI_ADAPTER_DESC1 description{};
                ThrowIfFailed(
                    adapter->GetDesc1(&description),
                    "IDXGIAdapter::GetDesc1");
                if ((description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0)
                {
                    continue;
                }

                foundHardwareAdapter = true;
                ComPtr<ID3D12Device> candidateDevice;
                lastDeviceResult = D3D12CreateDevice(
                    adapter.Get(),
                    D3D_FEATURE_LEVEL_11_0,
                    IID_PPV_ARGS(&candidateDevice));
                if (SUCCEEDED(lastDeviceResult))
                {
                    device_ = std::move(candidateDevice);
                    selectedAdapter = adapter;
                    selectedDescription = description;
                    break;
                }
            }

            if (!device_)
            {
                if (!foundHardwareAdapter)
                {
                    ThrowIfFailed(
                        DXGI_ERROR_NOT_FOUND,
                        "No hardware DXGI adapter was found");
                }
                ThrowIfFailed(
                    lastDeviceResult,
                    "D3D12CreateDevice on hardware adapters");
            }
        }
        else
        {
            ComPtr<IDXGIFactory6> factory6;
            const bool supportsGpuPreference =
                SUCCEEDED(factory_.As(&factory6));
            bool matchingAdapterFound = false;
            for (UINT adapterIndex = 0;; ++adapterIndex)
            {
                ComPtr<IDXGIAdapter1> adapter;
                const HRESULT enumResult = supportsGpuPreference
                    ? factory6->EnumAdapterByGpuPreference(
                          adapterIndex,
                          DXGI_GPU_PREFERENCE_UNSPECIFIED,
                          IID_PPV_ARGS(&adapter))
                    : factory_->EnumAdapters1(adapterIndex, &adapter);
                if (enumResult == DXGI_ERROR_NOT_FOUND)
                {
                    break;
                }
                ThrowIfFailed(
                    enumResult,
                    supportsGpuPreference
                        ? "IDXGIFactory6::EnumAdapterByGpuPreference"
                        : "IDXGIFactory::EnumAdapters1");

                DXGI_ADAPTER_DESC1 description{};
                ThrowIfFailed(
                    adapter->GetDesc1(&description),
                    "IDXGIAdapter::GetDesc1");
                if ((description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0 ||
                    !MatchesGpuSelection(description))
                {
                    continue;
                }

                matchingAdapterFound = true;
                ComPtr<ID3D12Device> candidateDevice;
                lastDeviceResult = D3D12CreateDevice(
                    adapter.Get(),
                    D3D_FEATURE_LEVEL_11_0,
                    IID_PPV_ARGS(&candidateDevice));
                if (SUCCEEDED(lastDeviceResult))
                {
                    device_ = std::move(candidateDevice);
                    selectedAdapter = adapter;
                    selectedDescription = description;
                    break;
                }
            }

            if (!matchingAdapterFound)
            {
                throw std::runtime_error(
                    std::string("The requested GPU ") +
                    GpuSelectionLabel(gpuSelection_) +
                    " was not found among the available DXGI adapters.");
            }
            if (!device_)
            {
                std::ostringstream message;
                message << "The requested GPU "
                        << GpuSelectionLabel(gpuSelection_)
                        << " was found, but D3D12CreateDevice failed (HRESULT=0x"
                        << std::hex << std::uppercase << std::setw(8)
                        << std::setfill('0')
                        << static_cast<unsigned long>(lastDeviceResult) << ')';
                throw std::runtime_error(message.str());
            }
        }

        selectedAdapterDescription_ = selectedDescription;
        if (diagnosticsEnabled_ || benchmarkEnabled_ || visualCaptureEnabled_)
        {
            std::wcout << L"[gpu] selected_adapter=\""
                       << selectedDescription.Description
                       << L"\" vendor_id=0x" << std::hex << std::uppercase
                       << std::setw(4) << std::setfill(L'0')
                       << selectedDescription.VendorId << std::dec
                       << L" dedicated_video_memory_bytes="
                       << selectedDescription.DedicatedVideoMemory
                       << L'\n' << std::flush;
        }

        D3D12_COMMAND_QUEUE_DESC queueDescription{};
        queueDescription.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        queueDescription.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
        queueDescription.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
        queueDescription.NodeMask = 0;
        ThrowIfFailed(
            device_->CreateCommandQueue(
                &queueDescription, IID_PPV_ARGS(&commandQueue_)),
            "ID3D12Device::CreateCommandQueue");
    }

    bool MatchesGpuSelection(const DXGI_ADAPTER_DESC1& description) const
    {
        std::wstring adapterName = description.Description;
        std::transform(
            adapterName.begin(),
            adapterName.end(),
            adapterName.begin(),
            [](wchar_t character) {
                return static_cast<wchar_t>(std::towlower(character));
            });

        if (gpuSelection_ == GpuSelection::Integrated)
        {
            return description.VendorId == 0x1002 &&
                adapterName.find(L"radeon") != std::wstring::npos &&
                adapterName.find(L"890m") != std::wstring::npos;
        }
        if (gpuSelection_ == GpuSelection::Discrete)
        {
            return description.VendorId == 0x10de &&
                adapterName.find(L"rtx 4060") != std::wstring::npos &&
                adapterName.find(L"laptop gpu") != std::wstring::npos;
        }
        return false;
    }

    const char* GpuSelectionLabel(GpuSelection selection) const
    {
        switch (selection)
        {
        case GpuSelection::Integrated:
            return "AMD Radeon 890M (iGPU)";
        case GpuSelection::Discrete:
            return "NVIDIA GeForce RTX 4060 Laptop GPU (dGPU)";
        case GpuSelection::Auto:
            return "auto-selected GPU";
        }
        return "requested GPU";
    }

    void CreateSwapChain()
    {
        DXGI_SWAP_CHAIN_DESC1 description{};
        description.Width = width_;
        description.Height = height_;
        description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        description.Stereo = FALSE;
        description.SampleDesc.Count = 1;
        description.SampleDesc.Quality = 0;
        description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        description.BufferCount = kFrameCount;
        description.Scaling = DXGI_SCALING_STRETCH;
        description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        description.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
        description.Flags = swapChainFlags_;

        ComPtr<IDXGISwapChain1> swapChain;
        ThrowIfFailed(
            factory_->CreateSwapChainForHwnd(
                commandQueue_.Get(), window_, &description, nullptr, nullptr,
                &swapChain),
            "IDXGIFactory::CreateSwapChainForHwnd");
        ThrowIfFailed(
            swapChain.As(&swapChain_),
            "IDXGISwapChain::QueryInterface(IDXGISwapChain3)");
        ThrowIfFailed(
            factory_->MakeWindowAssociation(window_, DXGI_MWA_NO_ALT_ENTER),
            "IDXGIFactory::MakeWindowAssociation");
    }

    void CheckTearingSupport()
    {
        tearingSupported_ = false;
        swapChainFlags_ = 0;

        ComPtr<IDXGIFactory5> factory5;
        if (SUCCEEDED(factory_.As(&factory5)))
        {
            BOOL allowTearing = FALSE;
            if (SUCCEEDED(factory5->CheckFeatureSupport(
                    DXGI_FEATURE_PRESENT_ALLOW_TEARING,
                    &allowTearing,
                    sizeof(allowTearing))))
            {
                tearingSupported_ = allowTearing != FALSE;
            }
        }

        if (tearingSupported_)
        {
            swapChainFlags_ = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
        }
        if (benchmarkEnabled_ || diagnosticsEnabled_ || visualCaptureEnabled_)
        {
            std::cout << "[dxgi] tearing="
                      << (tearingSupported_
                              ? "enabled"
                              : "unavailable; using non-tearing fallback")
                      << '\n';
        }
    }

    void CreateRenderTargetViews()
    {
        D3D12_DESCRIPTOR_HEAP_DESC heapDescription{};
        heapDescription.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        heapDescription.NumDescriptors =
            kFrameCount + (interactiveLayout_ ? 1U : 0U);
        heapDescription.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        heapDescription.NodeMask = 0;
        ThrowIfFailed(
            device_->CreateDescriptorHeap(
                &heapDescription, IID_PPV_ARGS(&renderTargetHeap_)),
            "ID3D12Device::CreateDescriptorHeap(RTV)");

        rtvDescriptorSize_ = device_->GetDescriptorHandleIncrementSize(
            D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        D3D12_CPU_DESCRIPTOR_HANDLE handle =
            renderTargetHeap_->GetCPUDescriptorHandleForHeapStart();

        for (UINT index = 0; index < kFrameCount; ++index)
        {
            ThrowIfFailed(
                swapChain_->GetBuffer(
                    index, IID_PPV_ARGS(&renderTargets_[index])),
                "IDXGISwapChain::GetBuffer");
            device_->CreateRenderTargetView(
                renderTargets_[index].Get(), nullptr, handle);
            handle.ptr += rtvDescriptorSize_;
        }
    }

    void CreatePersistentCanvas()
    {
        D3D12_HEAP_PROPERTIES heapProperties{};
        heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
        heapProperties.CreationNodeMask = 1;
        heapProperties.VisibleNodeMask = 1;
        D3D12_RESOURCE_DESC description{};
        description.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        description.Width = std::max(1U, width_);
        description.Height = std::max(1U, height_);
        description.DepthOrArraySize = 1;
        description.MipLevels = 1;
        description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        description.SampleDesc.Count = 1;
        description.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        description.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        const float clearColor[] = {1.0f, 1.0f, 1.0f, 1.0f};
        D3D12_CLEAR_VALUE clearValue{};
        clearValue.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        std::copy(std::begin(clearColor), std::end(clearColor), clearValue.Color);
        ThrowIfFailed(
            device_->CreateCommittedResource(
                &heapProperties, D3D12_HEAP_FLAG_NONE, &description,
                D3D12_RESOURCE_STATE_RENDER_TARGET, &clearValue,
                IID_PPV_ARGS(&persistentCanvas_)),
            "ID3D12Device::CreateCommittedResource(persistent canvas)");
        D3D12_CPU_DESCRIPTOR_HANDLE handle =
            renderTargetHeap_->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += static_cast<SIZE_T>(kFrameCount) * rtvDescriptorSize_;
        device_->CreateRenderTargetView(
            persistentCanvas_.Get(), nullptr, handle);
        persistentCanvasRtv_ = handle;
        canvasRenderedShapeCount_ = 0;
        canvasDirty_ = true;
        canvasNeedsClear_ = true;
    }

    void CreateCommandObjects()
    {
        for (auto& allocator : commandAllocators_)
        {
            ThrowIfFailed(
                device_->CreateCommandAllocator(
                    D3D12_COMMAND_LIST_TYPE_DIRECT,
                    IID_PPV_ARGS(&allocator)),
                "ID3D12Device::CreateCommandAllocator");
        }

        ThrowIfFailed(
            device_->CreateCommandList(
                0,
                D3D12_COMMAND_LIST_TYPE_DIRECT,
                commandAllocators_[0].Get(),
                nullptr,
                IID_PPV_ARGS(&commandList_)),
            "ID3D12Device::CreateCommandList");
        ThrowIfFailed(
            commandList_->Close(),
            "ID3D12GraphicsCommandList::Close(initial)");
        ThrowIfFailed(
            device_->CreateFence(
                0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_)),
            "ID3D12Device::CreateFence");
    }

    void ApplyResize()
    {
        resizePending_ = false;
        if (pendingWidth_ == width_ && pendingHeight_ == height_)
        {
            return;
        }

        if (rendererMode_ == RendererMode::Direct2D)
        {
            if (pendingWidth_ == 0 || pendingHeight_ == 0)
            {
                return;
            }
            width_ = pendingWidth_;
            height_ = pendingHeight_;
            UpdateCanvasWidth();
            direct2DBitmap_.Reset();
            direct2DBitmapBrush_.Reset();
            direct2DBitmapTarget_.Reset();
            if (direct2DTarget_)
            {
                const HRESULT resizeResult = direct2DTarget_->Resize(
                    D2D1::SizeU(width_, height_));
                if (resizeResult == D2DERR_RECREATE_TARGET)
                {
                    direct2DTarget_.Reset();
                }
                else
                {
                    ThrowIfFailed(
                        resizeResult,
                        "ID2D1HwndRenderTarget::Resize");
                }
            }
            if (!direct2DTarget_)
            {
                CreateDirect2DTarget();
            }
            RebuildDirect2DBitmapCanvas(false);
            return;
        }

        WaitForGpu();
        persistentCanvas_.Reset();
        for (auto& renderTarget : renderTargets_)
        {
            renderTarget.Reset();
        }

        ThrowIfFailed(
            swapChain_->ResizeBuffers(
                kFrameCount,
                pendingWidth_,
                pendingHeight_,
                DXGI_FORMAT_R8G8B8A8_UNORM,
                swapChainFlags_),
            "IDXGISwapChain::ResizeBuffers");

        width_ = pendingWidth_;
        height_ = pendingHeight_;
        UpdateCanvasWidth();
        frameFenceValues_.fill(0);
        CreateRenderTargetViews();
        if (interactiveLayout_)
        {
            CreatePersistentCanvas();
        }
        if (interactiveLayout_)
        {
            RebuildInteractiveGeometry();
        }
    }

    void WaitForFrame(UINT frameIndex)
    {
        const UINT64 fenceValue = frameFenceValues_[frameIndex];
        if (fenceValue != 0)
        {
            WaitForFenceValue(fenceValue);
        }
        ConsumeGpuTimestampForFrame(frameIndex);
    }

    void WaitForFenceValue(UINT64 value)
    {
        if (fence_->GetCompletedValue() >= value)
        {
            return;
        }

        ThrowIfFailed(
            fence_->SetEventOnCompletion(value, fenceEvent_),
            "ID3D12Fence::SetEventOnCompletion");
        const DWORD waitResult = WaitForSingleObject(fenceEvent_, INFINITE);
        if (waitResult != WAIT_OBJECT_0)
        {
            throw std::runtime_error("WaitForSingleObject(fence) failed");
        }
    }

    void CreateGpuTimestampQueries()
    {
        D3D12_QUERY_HEAP_DESC heapDescription{};
        heapDescription.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
        heapDescription.Count = kFrameCount * 2;
        ThrowIfFailed(
            device_->CreateQueryHeap(
                &heapDescription, IID_PPV_ARGS(&gpuTimestampQueryHeap_)),
            "ID3D12Device::CreateQueryHeap(timestamp)");
        ThrowIfFailed(
            commandQueue_->GetTimestampFrequency(&gpuTimestampFrequency_),
            "ID3D12CommandQueue::GetTimestampFrequency");
        if (gpuTimestampFrequency_ == 0)
        {
            throw std::runtime_error(
                "ID3D12CommandQueue returned a zero timestamp frequency");
        }

        D3D12_HEAP_PROPERTIES heapProperties{};
        heapProperties.Type = D3D12_HEAP_TYPE_READBACK;
        heapProperties.CreationNodeMask = 1;
        heapProperties.VisibleNodeMask = 1;
        D3D12_RESOURCE_DESC resourceDescription{};
        resourceDescription.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        resourceDescription.Width =
            static_cast<UINT64>(kFrameCount) * 2 * sizeof(UINT64);
        resourceDescription.Height = 1;
        resourceDescription.DepthOrArraySize = 1;
        resourceDescription.MipLevels = 1;
        resourceDescription.SampleDesc.Count = 1;
        resourceDescription.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        ThrowIfFailed(
            device_->CreateCommittedResource(
                &heapProperties, D3D12_HEAP_FLAG_NONE, &resourceDescription,
                D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                IID_PPV_ARGS(&gpuTimestampReadback_)),
            "ID3D12Device::CreateCommittedResource(timestamp readback)");
        D3D12_RANGE readRange{0, resourceDescription.Width};
        ThrowIfFailed(
            gpuTimestampReadback_->Map(0, &readRange, &gpuTimestampMapped_),
            "Map(timestamp readback)");
        gpuTimestampEnabled_ = true;
    }

    void ConsumeGpuTimestampForFrame(UINT frameIndex)
    {
        if (!gpuTimestampEnabled_ || !gpuTimestampPending_[frameIndex])
        {
            return;
        }
        const auto* values = static_cast<const UINT64*>(gpuTimestampMapped_);
        const UINT64 start = values[frameIndex * 2];
        const UINT64 stop = values[frameIndex * 2 + 1];
        if (stop > start)
        {
            gpuTimestampTicksTotal_ += stop - start;
            ++gpuTimestampFrameCount_;
            if (rendererBenchmarkGpuSampling_)
            {
                rendererBenchmarkGpuTicks_.push_back(stop - start);
            }
        }
        else if (rendererBenchmarkGpuSampling_)
        {
            ++rendererBenchmarkGpuInvalidSamples_;
        }
        gpuTimestampPending_[frameIndex] = false;
    }

    HWND window_ = nullptr;
    ComPtr<ID2D1Factory> direct2DFactory_;
    ComPtr<ID2D1HwndRenderTarget> direct2DTarget_;
    ComPtr<ID2D1BitmapRenderTarget> direct2DBitmapTarget_;
    ComPtr<ID2D1Bitmap> direct2DBitmap_;
    ComPtr<ID2D1SolidColorBrush> direct2DBitmapBrush_;
    ComPtr<IDXGIFactory4> factory_;
    DXGI_ADAPTER_DESC1 selectedAdapterDescription_{};
    ComPtr<ID3D12Device> device_;
    ComPtr<ID3D12CommandQueue> commandQueue_;
    ComPtr<IDXGISwapChain3> swapChain_;
    ComPtr<ID3D12DescriptorHeap> renderTargetHeap_;
    std::array<ComPtr<ID3D12Resource>, kFrameCount> renderTargets_;
    ComPtr<ID3D12Resource> persistentCanvas_;
    D3D12_CPU_DESCRIPTOR_HANDLE persistentCanvasRtv_{};
    std::array<ComPtr<ID3D12CommandAllocator>, kFrameCount> commandAllocators_;
    ComPtr<ID3D12GraphicsCommandList> commandList_;
    ComPtr<ID3D12RootSignature> rootSignature_;
    ComPtr<ID3D12PipelineState> pipelineState_;
    ComPtr<ID3D12RootSignature> shaderRootSignature_;
    ComPtr<ID3D12PipelineState> shaderPipelineState_;
    ComPtr<ID3D12Resource> vertexBuffer_;
    ComPtr<ID3D12Resource> indexBuffer_;
    ComPtr<ID3D12Resource> shaderInstanceBuffer_;
    D3D12_VERTEX_BUFFER_VIEW shaderInstanceBufferView_{};
    UINT shaderInstanceBufferCapacityBytes_ = 0;
    std::size_t lastUploadedShaderInstanceCount_ = 0;
    std::vector<CircleInstance> shaderCircleInstances_;
    std::vector<UINT> shapeCircleInstanceIndices_;
    ComPtr<ID3D12QueryHeap> gpuTimestampQueryHeap_;
    ComPtr<ID3D12Resource> gpuTimestampReadback_;
    void* gpuTimestampMapped_ = nullptr;
    UINT64 gpuTimestampFrequency_ = 0;
    UINT64 gpuTimestampTicksTotal_ = 0;
    UINT64 gpuTimestampFrameCount_ = 0;
    std::array<bool, kFrameCount> gpuTimestampPending_{};
    bool gpuTimestampEnabled_ = false;
    bool visualCaptureEnabled_ = false;
    ComPtr<ID3D12Resource> visualReadback_;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT visualReadbackFootprint_{};
    UINT64 visualReadbackSize_ = 0;
    UINT lastFrameDrawCallCount_ = 0;
    UINT lastFrameRenderedCircleCount_ = 0;
    RendererFrameSample lastRendererFrameSample_{};
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};
    D3D12_INDEX_BUFFER_VIEW indexBufferView_{};
    std::vector<CircleParameters> circleParameters_;
    std::vector<Vertex> vertices_;
    std::vector<std::uint16_t> indices_;
    std::vector<CircleDraw> circleDraws_;
    std::vector<DrawingShape> drawingShapes_;
    std::vector<RetiredGeometryBuffers> retiredGeometryBuffers_;
    std::mt19937 randomGenerator_{std::random_device{}()};
    bool pendingPoint_ = false;
    int pendingX_ = 0;
    int pendingY_ = 0;
    int pendingThickness_ = 1;
    COLORREF pendingColor_ = RGB(0, 0, 0);
    ULONGLONG speedWindowStartMs_ = 0;
    UINT64 generatedInSpeedWindow_ = 0;
    UINT64 renderedInSpeedWindow_ = 0;
    UINT64 pendingGeneratedShapesForPresent_ = 0;
    UINT64 generatedShapeCountThisRun_ = 0;
    UINT slowWindowCount_ = 0;
    UINT fastWindowCount_ = 0;
    ULONGLONG drawingRunStartMs_ = 0;
    ULONGLONG lastBatchingModeChangeMs_ = 0;
    bool batchingEnabled_ = false;
    bool hasBatchingModeChanged_ = false;
    UINT requestedCircleCount_ = kSingleCircleCount;
    ComPtr<ID3D12Fence> fence_;
    HANDLE fenceEvent_ = nullptr;
    std::array<UINT64, kFrameCount> frameFenceValues_{};
    UINT64 nextFenceValue_ = 1;
    UINT rtvDescriptorSize_ = 0;
    UINT swapChainFlags_ = 0;
    UINT vertexBufferCapacityBytes_ = 0;
    UINT indexBufferCapacityBytes_ = 0;
    std::size_t lastUploadedVertexCount_ = 0;
    std::size_t lastUploadedIndexCount_ = 0;
    UINT width_ = 0;
    UINT height_ = 0;
    UINT canvasWidth_ = 0;
    UINT pendingWidth_ = 0;
    UINT pendingHeight_ = 0;
    bool resizePending_ = false;
    bool minimized_ = false;
    bool commandsSubmitted_ = false;
    bool benchmarkEnabled_ = false;
    bool renderBenchmarkEnabled_ = false;
    bool diagnosticsEnabled_ = false;
    bool pipelineComparisonEnabled_ = false;
    RendererMode rendererMode_ = RendererMode::Legacy;
    GpuSelection gpuSelection_ = GpuSelection::Auto;
    bool pipelineComparisonActive_ = false;
    bool interactiveGeometryEnabled_ = true;
    bool debugLayerEnabled_ = false;
    bool interactiveLayout_ = false;
    bool tearingSupported_ = false;
    bool canvasDirty_ = true;
    bool canvasNeedsClear_ = true;
    std::size_t canvasRenderedShapeCount_ = 0;
    HWND mainWindow_ = nullptr;
    HWND canvasWindow_ = nullptr;
    HFONT font_ = nullptr;
    std::array<HWND, 21> controls_{};
    std::array<COLORREF, 16> customColors_{};
    int thickness_ = 1;
    COLORREF selectedColor_ = RGB(0, 0, 0);
    UINT drawIntervalMs_ = 100;
    bool circleMode_ = false;
    bool randomDrawing_ = false;
    std::size_t objectCount_ = 0;
    std::size_t objectMemoryBytes_ = 0;
    UINT actualObjectsPerSecond_ = 0;
    LARGE_INTEGER diagnosticFrequency_{};
    ULONGLONG diagnosticWindowStartMs_ = 0;
    UINT64 diagnosticFrameCount_ = 0;
    UINT64 diagnosticObjectCount_ = 0;
    UINT64 diagnosticAddTicks_ = 0;
    UINT64 diagnosticFenceWaitTicks_ = 0;
    UINT64 diagnosticRecordTicks_ = 0;
    UINT64 diagnosticPresentTicks_ = 0;
    UINT64 diagnosticSignalTicks_ = 0;
    UINT64 direct2DDiagnosticEndDrawCycles_ = 0;
    UINT64 direct2DDiagnosticApplicationWaitCount_ = 0;
    UINT64 direct2DDiagnosticApplicationWaitTicks_ = 0;
    FILETIME direct2DDiagnosticThreadKernelBaseline_{};
    FILETIME direct2DDiagnosticThreadUserBaseline_{};
    ULONGLONG direct2DDiagnosticThreadCycleBaseline_ = 0;
    DrawingTimerMode drawingTimerMode_ = DrawingTimerMode::WindowTimer;
    LARGE_INTEGER drawingQpcFrequency_{};
    LARGE_INTEGER drawingStartQpc_{};
    LARGE_INTEGER nextDrawingDeadline_{};
    LONGLONG drawingIntervalTicks_ = 0;
    UINT64 qpcSkippedDeadlines_ = 0;
    bool timerComparisonActive_ = false;
    TimerComparisonPhase timerComparisonPhase_ = TimerComparisonPhase::None;
    std::size_t timerComparisonCaseIndex_ = 0;
    LARGE_INTEGER timerComparisonFrequency_{};
    LARGE_INTEGER timerComparisonPhaseDeadline_{};
    LARGE_INTEGER timerComparisonMeasurementStart_{};
    LARGE_INTEGER timerComparisonMeasurementEnd_{};
    UINT64 timerComparisonAddedCount_ = 0;
    LONGLONG timerComparisonLastAdditionTicks_ = 0;
    std::vector<LONGLONG> timerComparisonIntervalsTicks_;
    std::vector<LONGLONG> pipelineObjectElapsedTicks_;
    UINT64 pipelineObjectElapsedTickSum_ = 0;
    std::vector<ULONGLONG> pipelineObjectCycles_;
    UINT64 pipelineObjectCycleSum_ = 0;
    UINT64 pipelinePresentCount_ = 0;
    UINT64 pipelineGeometryPrepTicks_ = 0;
    UINT64 pipelineGeometryUploadTicks_ = 0;
    ULONGLONG pipelineThreadCyclesStart_ = 0;
    ULONGLONG pipelineThreadCyclesEnd_ = 0;
    UINT64 rendererBenchmarkRecordCycles_ = 0;
    UINT64 rendererBenchmarkExecuteCycles_ = 0;
    UINT64 rendererBenchmarkRecordElapsedTicks_ = 0;
    UINT64 rendererBenchmarkExecuteElapsedTicks_ = 0;
    UINT64 rendererBenchmarkPresentReturns_ = 0;
    UINT64 rendererBenchmarkDrawCalls_ = 0;
    UINT64 rendererBenchmarkRenderedCircles_ = 0;
    std::vector<RendererFrameSample> rendererBenchmarkFrameSamples_;
    std::vector<UINT64> rendererBenchmarkGpuTicks_;
    std::vector<Direct2DFrameSample> direct2DBenchmarkSamples_;
    bool direct2DBenchmarkActive_ = false;
    UINT64 direct2DScenePreparationTicks_ = 0;
    bool circleThroughputBenchmarkEnabled_ = false;
    bool circleThroughputBenchmarkActive_ = false;
    UINT circleThroughputBatchSize_ = kMaximumAdaptiveBatchSize;
    UINT circleThroughputTargetCount_ = 0;
    std::uint64_t circleThroughputSceneHash_ = 0;
    LARGE_INTEGER circleThroughputQpcFrequency_{};
    UINT64 circleThroughputScenePreparationTicks_ = 0;
    std::vector<DrawingShape> circleThroughputShapes_;
    CircleThroughputMetrics circleThroughputMetrics_;
    std::size_t direct2DDrawnShapeCount_ = 0;
    UINT64 direct2DNewlyRenderedCircleCount_ = 0;
    UINT64 direct2DCanvasUpdateCount_ = 0;
    UINT64 direct2DCanvasUpdateTicks_ = 0;
    UINT64 direct2DDiagnosticNewCircleCount_ = 0;
    UINT64 direct2DDiagnosticCanvasUpdateCount_ = 0;
    UINT64 direct2DDiagnosticCanvasUpdateTicks_ = 0;
    std::wstring benchmarkOutputFile_;
    std::string benchmarkOutputJson_;
    bool rendererBenchmarkGpuSampling_ = false;
    UINT64 rendererBenchmarkGpuQueriesSubmitted_ = 0;
    UINT64 rendererBenchmarkGpuInvalidSamples_ = 0;
    LARGE_INTEGER rendererBenchmarkMeasurementStart_{};
    LARGE_INTEGER rendererBenchmarkMeasurementEnd_{};
    bool rendererBenchmarkActive_ = false;
    PipelineComparisonScenario pipelineScenario_ = PipelineComparisonScenario::RendererIdle;
    FILETIME pipelineCpuStartCreation_{}, pipelineCpuStartExit_{}, pipelineCpuStartKernel_{}, pipelineCpuStartUser_{};
    FILETIME pipelineCpuEndCreation_{}, pipelineCpuEndExit_{}, pipelineCpuEndKernel_{}, pipelineCpuEndUser_{};
    SIZE_T privateMemoryBaseline_ = 0;
};

CommandLineOptions ParseCommandLine()
{
    int argumentCount = 0;
    LPWSTR* rawArguments =
        CommandLineToArgvW(GetCommandLineW(), &argumentCount);
    if (rawArguments == nullptr)
    {
        throw std::runtime_error("CommandLineToArgvW failed");
    }
    struct CommandLineArguments
    {
        LPWSTR* values;
        ~CommandLineArguments() { LocalFree(values); }
    } arguments{rawArguments};

    CommandLineOptions options;
    for (int index = 1; index < argumentCount; ++index)
    {
        const std::wstring argument = arguments.values[index];
        const auto nextValue = [&]() {
            if (index + 1 >= argumentCount)
            {
                throw std::runtime_error(
                    "A command-line option is missing its value.");
            }
            return std::wstring(arguments.values[++index]);
        };
        if (argument == L"--help" || argument == L"-h")
        {
            options.showHelp = true;
            continue;
        }
        if (argument == L"--benchmark")
        {
            options.benchmark = true;
            continue;
        }
        if (argument == L"--render-benchmark")
        {
            options.renderBenchmark = true;
            continue;
        }
        if (argument == L"--circle-throughput-benchmark")
        {
            options.circleThroughputBenchmark = true;
            continue;
        }
        if (argument == L"--circle-batch-size")
        {
            const std::wstring value = nextValue();
            if (value.empty() ||
                std::any_of(value.begin(), value.end(), [](wchar_t character) {
                    return character < L'0' || character > L'9';
                }))
            {
                throw std::runtime_error(
                    "--circle-batch-size requires a positive integer from 1 to 512.");
            }
            const unsigned long parsedValue = std::stoul(value);
            if (parsedValue == 0 || parsedValue > kMaximumAdaptiveBatchSize)
            {
                throw std::runtime_error(
                    "--circle-batch-size must be between 1 and 512.");
            }
            options.circleBatchSize = static_cast<UINT>(parsedValue);
            options.circleBatchSizeSpecified = true;
            continue;
        }
        if (argument == L"--gpu-benchmark")
        {
            options.gpuBenchmark = true;
            continue;
        }
        if (argument == L"--visual-capture")
        {
            options.visualCapture = true;
            continue;
        }
        if (argument == L"--renderer")
        {
            const std::wstring value = nextValue();
            if (value != L"legacy" && value != L"shader" &&
                value != L"direct2d")
            {
                throw std::runtime_error(
                    "--renderer accepts only legacy, shader, or direct2d.");
            }
            options.rendererMode =
                value == L"shader" ? RendererMode::Shader :
                value == L"direct2d" ? RendererMode::Direct2D :
                                       RendererMode::Legacy;
            continue;
        }
        if (argument == L"--gpu")
        {
            const std::wstring value = nextValue();
            if (value != L"auto" && value != L"igpu" && value != L"dgpu")
            {
                throw std::runtime_error(
                    "--gpu accepts only auto, igpu, or dgpu.");
            }
            options.gpuSelection = value == L"igpu"
                ? GpuSelection::Integrated
                : value == L"dgpu" ? GpuSelection::Discrete
                                   : GpuSelection::Auto;
            continue;
        }
        if (argument == L"--output-file")
        {
            if (!options.outputFile.empty())
            {
                throw std::runtime_error(
                    "--output-file may be specified only once.");
            }
            options.outputFile = nextValue();
            if (options.outputFile.empty())
            {
                throw std::runtime_error(
                    "--output-file requires a non-empty path.");
            }
            continue;
        }
        if (argument == L"--diagnostics")
        {
            options.diagnostics = true;
            continue;
        }
        if (argument == L"--timer-compare")
        {
            options.timerComparison = true;
            continue;
        }
        if (argument == L"--pipeline-compare")
        {
            options.pipelineComparison = true;
            continue;
        }
        if (argument == L"--timer-mode")
        {
            const std::wstring value = nextValue();
            if (value != L"wm" && value != L"qpc")
            {
                throw std::runtime_error(
                    "--timer-mode accepts only wm or qpc.");
            }
            options.drawingTimerMode = value == L"qpc"
                ? DrawingTimerMode::Qpc
                : DrawingTimerMode::WindowTimer;
            continue;
        }
        if (argument != L"--circles")
        {
            throw std::runtime_error(
                "Unknown argument. Use --help for usage.");
        }

        const std::wstring value = nextValue();
        if (value != L"1" && value != L"128" && value != L"1000" &&
            value != L"5000" && value != L"10000")
        {
            throw std::runtime_error(
                "The --circles option accepts 1, 128, 1000, 5000, or 10000.");
        }
        options.circleCount = static_cast<UINT>(std::stoul(value));
        options.circlesSpecified = true;
    }
    if (options.gpuBenchmark && !options.circlesSpecified)
    {
        throw std::runtime_error(
            "--gpu-benchmark requires an explicit --circles N value.");
    }
    if (options.circleThroughputBenchmark && !options.circlesSpecified)
    {
        throw std::runtime_error(
            "--circle-throughput-benchmark requires an explicit --circles N value.");
    }
    if (options.circleBatchSizeSpecified &&
        !options.circleThroughputBenchmark)
    {
        throw std::runtime_error(
            "--circle-batch-size is supported only with --circle-throughput-benchmark.");
    }
    if (options.circleThroughputBenchmark &&
        (options.benchmark || options.renderBenchmark || options.gpuBenchmark ||
         options.timerComparison || options.pipelineComparison ||
         options.visualCapture || options.diagnostics))
    {
        throw std::runtime_error(
            "--circle-throughput-benchmark cannot be combined with other benchmark, comparison, capture, or diagnostic modes.");
    }
    if (options.gpuBenchmark &&
        (options.benchmark || options.renderBenchmark ||
         options.timerComparison || options.pipelineComparison ||
         options.visualCapture))
    {
        throw std::runtime_error(
            "--gpu-benchmark cannot be combined with --benchmark, --render-benchmark, comparison, or --visual-capture modes.");
    }
    if (options.timerComparison &&
        (options.benchmark || options.renderBenchmark || options.circlesSpecified))
    {
        throw std::runtime_error(
            "--timer-compare cannot be combined with --benchmark or --circles.");
    }
    if (options.pipelineComparison &&
        (options.benchmark || options.renderBenchmark || options.circlesSpecified || options.timerComparison))
    {
        throw std::runtime_error(
            "--pipeline-compare cannot be combined with --benchmark, --render-benchmark, --circles, or --timer-compare.");
    }
    if (options.visualCapture &&
        (options.benchmark || options.renderBenchmark || options.timerComparison ||
         options.pipelineComparison || options.circlesSpecified))
    {
        throw std::runtime_error(
            "--visual-capture cannot be combined with benchmark, comparison, or --circles options.");
    }
    if (options.renderBenchmark && (options.benchmark || options.timerComparison))
    {
        throw std::runtime_error(
            "--render-benchmark cannot be combined with --benchmark or --timer-compare.");
    }
    if (!options.outputFile.empty() && !options.renderBenchmark &&
        !options.circleThroughputBenchmark)
    {
        throw std::runtime_error(
            "--output-file is supported only with --render-benchmark or --circle-throughput-benchmark.");
    }
    if (options.rendererMode == RendererMode::Direct2D &&
        (options.benchmark || options.gpuBenchmark ||
         options.pipelineComparison || options.visualCapture))
    {
        throw std::runtime_error(
            "Direct2D supports interactive rendering, --render-benchmark, and "
            "--circle-throughput-benchmark; "
            "--benchmark, --gpu-benchmark, --pipeline-compare, and "
            "--visual-capture use D3D12-specific infrastructure.");
    }
    return options;
}

void AttachOutputConsole()
{
    if (!AttachConsole(ATTACH_PARENT_PROCESS) && GetConsoleWindow() == nullptr &&
        !AllocConsole())
    {
        throw std::runtime_error(
            "Could not attach or allocate a console for output");
    }

    const auto connectStream = [](DWORD standardHandleId,
                                  const char* device,
                                  FILE* stream) {
        const HANDLE handle = GetStdHandle(standardHandleId);
        if (handle != nullptr && handle != INVALID_HANDLE_VALUE &&
            GetFileType(handle) != FILE_TYPE_UNKNOWN)
        {
            return;
        }

        FILE* connectedStream = nullptr;
        if (freopen_s(&connectedStream, device, "w", stream) != 0)
        {
            throw std::runtime_error(
                std::string("Could not connect standard stream: ") + device);
        }
    };
    connectStream(STD_OUTPUT_HANDLE, "CONOUT$", stdout);
    connectStream(STD_ERROR_HANDLE, "CONOUT$", stderr);
    if (GetStdHandle(STD_OUTPUT_HANDLE) == INVALID_HANDLE_VALUE)
    {
        throw std::runtime_error(
            "Could not connect standard output to the console");
    }
    std::cout.clear();
    std::cerr.clear();
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    D3D12App* app = reinterpret_cast<D3D12App*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));

    if (message == WM_NCCREATE)
    {
        const auto* create =
            reinterpret_cast<const CREATESTRUCTW*>(lParam);
        app = static_cast<D3D12App*>(create->lpCreateParams);
        SetWindowLongPtrW(
            window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
        app->AttachMainWindow(window);
    }

    if (app != nullptr)
    {
        if (message == WM_SIZE)
        {
            app->LayoutFromParent(
                static_cast<UINT>(LOWORD(lParam)),
                static_cast<UINT>(HIWORD(lParam)));
            return 0;
        }
        if (message == WM_COMMAND)
        {
            app->HandleControlCommand(
                LOWORD(wParam), HIWORD(wParam));
            return 0;
        }
        if (message == WM_HSCROLL)
        {
            app->HandleHorizontalScroll(reinterpret_cast<HWND>(lParam));
            return 0;
        }
        if (message == WM_TIMER && wParam == kInformationTimerId)
        {
            app->HandleInformationTimer();
            return 0;
        }
        if (message == WM_TIMER && wParam == kDrawingTimerId)
        {
            app->HandleWindowDrawingTimer();
            return 0;
        }
        if (message == WM_ERASEBKGND)
        {
            static HBRUSH panelBrush = CreateSolidBrush(RGB(240, 240, 240));
            RECT clientRect{};
            if (GetClientRect(window, &clientRect))
            {
                FillRect(
                    reinterpret_cast<HDC>(wParam),
                    &clientRect,
                    panelBrush);
            }
            return 1;
        }
        if (message == WM_CTLCOLORSTATIC)
        {
            static HBRUSH panelBrush = CreateSolidBrush(RGB(240, 240, 240));
            SetBkMode(reinterpret_cast<HDC>(wParam), TRANSPARENT);
            SetTextColor(reinterpret_cast<HDC>(wParam), RGB(0, 0, 0));
            return reinterpret_cast<LRESULT>(panelBrush);
        }
    }

    if (message == WM_DESTROY)
    {
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(window, message, wParam, lParam);
}

LRESULT CALLBACK CanvasWindowProc(
    HWND window,
    UINT message,
    WPARAM wParam,
    LPARAM lParam)
{
    if (message == WM_NCCREATE)
    {
        const auto* create =
            reinterpret_cast<const CREATESTRUCTW*>(lParam);
        auto* app = static_cast<D3D12App*>(create->lpCreateParams);
        SetWindowLongPtrW(
            window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
        app->AttachWindow(window);
    }

    auto* app = reinterpret_cast<D3D12App*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));
    if (app != nullptr && message == WM_LBUTTONDOWN)
    {
        app->HandleCanvasClick(
            static_cast<int>(GET_X_LPARAM(lParam)),
            static_cast<int>(GET_Y_LPARAM(lParam)));
        return 0;
    }
    if (app != nullptr && message == WM_SIZE)
    {
        app->OnResize(
            static_cast<UINT>(LOWORD(lParam)),
            static_cast<UINT>(HIWORD(lParam)));
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

void RegisterCanvasWindowClass(HINSTANCE instance)
{
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = CanvasWindowProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.lpszClassName = kCanvasClassName;
    if (!RegisterClassExW(&windowClass))
    {
        throw std::runtime_error("RegisterClassExW(canvas) failed");
    }
}
} // namespace

int WINAPI wWinMain(
    HINSTANCE instance,
    HINSTANCE,
    PWSTR,
    int showCommand)
{
    D3D12App app;
    HWND window = nullptr;

    try
    {
        const CommandLineOptions options = ParseCommandLine();
        if (options.showHelp)
        {
            MessageBoxW(
                nullptr,
                L"Usage: main.exe [--renderer legacy|shader|direct2d] [--gpu auto|igpu|dgpu] [--circles 1|128|1000|5000|10000] [--benchmark|--render-benchmark|--circle-throughput-benchmark|--gpu-benchmark|--visual-capture] [--circle-batch-size 1..512] [--output-file path.json] [--diagnostics] [--timer-mode wm|qpc] [--timer-compare] [--pipeline-compare]\n"
                L"Interactive mode starts with an empty canvas.\n"
                L"--benchmark defaults to one circle if --circles is omitted.\n"
                L"The 128-circle scene is generated with a fixed seed.\n"
                L"--circles also accepts 1000, 5000, and 10000 for renderer scaling tests.\n"
                L"--benchmark runs five CPU command-submission measurements.\n"
                L"--render-benchmark presents the selected renderer for 3 s warm-up and 5 s measurement.\n"
                L"--circle-throughput-benchmark incrementally adds the seed-fixed scene in batches and measures added/drawn circles per second; requires --circles N.\n"
                L"--circle-batch-size N selects a fixed throughput batch from 1 to 512 (default 512).\n"
                L"--output-file also writes the circle-throughput result as UTF-8 JSON.\n"
                L"--output-file path.json writes the successful --render-benchmark result as UTF-8 JSON with status and exit_code.\n"
                L"--renderer direct2d uses ID2D1HwndRenderTarget; its render benchmark reports CPU-side draw issue and EndDraw timings, not GPU execution time.\n"
                L"--gpu-benchmark requires --circles N, prepares a fixed scene, warms up for 3 s, and measures GPU draw timestamps for 10 s.\n"
                L"--visual-capture writes a deterministic diagnostic BMP.\n"
                L"--renderer shader draws circles with an instanced procedural HLSL pipeline; legacy remains the default.\n"
                L"--gpu auto preserves the default adapter selection; igpu selects AMD Radeon 890M; dgpu selects NVIDIA GeForce RTX 4060 Laptop GPU.\n"
                L"Direct2D HWND targets do not expose or guarantee --gpu adapter selection; diagnostics report the adapter as unknown.\n"
                L"--diagnostics reports Present, frame-wait, and object-add timings.\n"
                L"For Legacy, --diagnostics also reports CPU vector size/capacity, current and retired D3D12 geometry-buffer capacities, and process PrivateUsage once per second under [diag][legacy-memory].\n"
                L"For Direct2D, --diagnostics reports EndDraw CPU-time estimate and elapsed time separately, plus explicit WaitMessage time; neither is GPU time.\n"
                L"--timer-mode qpc uses QPC scheduling instead of WM_TIMER.\n"
                L"--timer-compare runs both modes at 10, 5, and 1 ms.\n"
                L"--pipeline-compare measures idle, CPU-only add, and full D3D12 at 10, 5, and 1 ms; 3 s warm-up and 5 s measurement per case.\n"
                L"Use --renderer legacy --render-benchmark --circles 128 and repeat with --renderer shader for a renderer comparison.",
                L"Native D3D12 circle test",
                MB_OK | MB_ICONINFORMATION);
            return 0;
        }
        const UINT circleCount =
            (options.benchmark || options.renderBenchmark) && !options.circlesSpecified
            ? kSingleCircleCount
            : options.circleCount;
        app.SetCircleCount(circleCount);
        app.SetBenchmarkEnabled(
            options.benchmark || options.renderBenchmark || options.gpuBenchmark ||
            options.circleThroughputBenchmark);
        app.SetRendererMode(options.rendererMode);
        app.SetGpuSelection(options.gpuSelection);
        app.SetRenderBenchmarkEnabled(options.renderBenchmark);
        app.SetCircleThroughputBenchmarkEnabled(
            options.circleThroughputBenchmark);
        app.SetCircleThroughputBatchSize(options.circleBatchSize);
        app.SetPipelineComparisonEnabled(options.pipelineComparison);
        app.SetDiagnosticsEnabled(options.diagnostics);
        app.SetDrawingTimerMode(options.drawingTimerMode);
        app.SetInteractiveLayout(
            !options.benchmark && !options.renderBenchmark &&
            !options.gpuBenchmark && !options.visualCapture);
        app.SetVisualCaptureEnabled(options.visualCapture);
        app.SetBenchmarkOutputFile(options.outputFile);
        if (options.benchmark || options.renderBenchmark || options.gpuBenchmark ||
            options.visualCapture || options.diagnostics ||
            options.timerComparison || options.pipelineComparison ||
            options.circleThroughputBenchmark)
        {
            if (options.outputFile.empty())
            {
                AttachOutputConsole();
            }
        }

        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.style = CS_HREDRAW | CS_VREDRAW;
        windowClass.lpfnWndProc = WindowProc;
        windowClass.hInstance = instance;
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        windowClass.lpszClassName = kWindowClassName;
        if (!RegisterClassExW(&windowClass))
        {
            throw std::runtime_error("RegisterClassExW failed");
        }
        if (!options.benchmark && !options.renderBenchmark &&
            !options.circleThroughputBenchmark && !options.gpuBenchmark &&
            !options.visualCapture)
        {
            RegisterCanvasWindowClass(instance);
        }

        RECT outerRect{0, 0, 800, 600};
        if (!AdjustWindowRectEx(
                &outerRect,
                WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                FALSE,
                0))
        {
            throw std::runtime_error("AdjustWindowRectEx failed");
        }

        window = CreateWindowExW(
            0,
            kWindowClassName,
            kWindowTitle,
            WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            outerRect.right - outerRect.left,
            outerRect.bottom - outerRect.top,
            nullptr,
            nullptr,
            instance,
            &app);
        if (window == nullptr)
        {
            throw std::runtime_error("CreateWindowExW failed");
        }

        if (options.benchmark || options.renderBenchmark ||
            options.circleThroughputBenchmark || options.gpuBenchmark ||
            options.visualCapture)
        {
            app.AttachWindow(window);
        }
        else
        {
            RECT clientRect{};
            if (!GetClientRect(window, &clientRect))
            {
                throw std::runtime_error("GetClientRect(main window) failed");
            }
            const int canvasWidth = std::max(
                1,
                static_cast<int>(clientRect.right - clientRect.left) -
                    static_cast<int>(kPanelWidth));
            HWND canvas = CreateWindowExW(
                0,
                kCanvasClassName,
                L"",
                WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
                static_cast<int>(kPanelWidth),
                0,
                canvasWidth,
                clientRect.bottom - clientRect.top,
                window,
                nullptr,
                instance,
                &app);
            if (canvas == nullptr)
            {
                throw std::runtime_error("CreateWindowExW(canvas) failed");
            }
            app.CreateInteractiveControls(window);
        }

        app.Initialize();
        if (options.benchmark)
        {
            app.RunBenchmark();
            app.Shutdown();
            return 0;
        }

        if (options.gpuBenchmark)
        {
            app.RunGpuBenchmark();
            app.Shutdown();
            return 0;
        }

        if (options.renderBenchmark)
        {
            ShowWindow(window, showCommand);
            UpdateWindow(window);
            app.RunRendererBenchmark();
            app.Shutdown();
            app.WriteBenchmarkOutputFile();
            return 0;
        }
        if (options.circleThroughputBenchmark)
        {
            ShowWindow(window, showCommand);
            UpdateWindow(window);
            app.Render();
            if (options.rendererMode != RendererMode::Direct2D)
            {
                app.WaitForGpu();
            }
            app.RunCircleThroughputBenchmark();
            app.Shutdown();
            app.WriteBenchmarkOutputFile();
            return 0;
        }
        if (options.visualCapture)
        {
            const std::wstring output =
                options.rendererMode == RendererMode::Legacy
                    ? L"visual-legacy-capture.bmp"
                    : L"visual-shader-capture.bmp";
            app.RunVisualCapture(output);
            app.Shutdown();
            return 0;
        }

        ShowWindow(window, showCommand);
        UpdateWindow(window);
        if (!options.benchmark && !options.renderBenchmark &&
            !options.circleThroughputBenchmark &&
            SetTimer(window, kInformationTimerId, 1000, nullptr) == 0)
        {
            throw std::runtime_error("SetTimer(information panel) failed");
        }
        if (options.timerComparison)
        {
            app.BeginTimerComparison();
        }
        if (options.pipelineComparison)
        {
            app.BeginPipelineComparison();
        }

        MSG message{};
        bool running = true;
        while (running)
        {
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
            {
                if (message.message == WM_QUIT)
                {
                    running = false;
                    break;
                }
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }

            if (running)
            {
                app.UpdateTimerComparison();
                app.PumpQpcDrawingTimer();
                if (IsIconic(window))
                {
                    app.WaitForWindowMessage();
                }
                else
                {
                    app.Render();
                }
            }
        }

        app.Shutdown();
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Native D3D12 benchmark failed: "
                  << error.what() << std::endl;
        MessageBoxA(
            window,
            error.what(),
            "Native D3D12 benchmark failed",
            MB_OK | MB_ICONERROR);
        try
        {
            app.Shutdown();
        }
        catch (const std::exception& cleanupError)
        {
            std::cerr << "D3D12 cleanup failed: "
                      << cleanupError.what() << std::endl;
        }
        if (window != nullptr && IsWindow(window))
        {
            DestroyWindow(window);
        }
        return 1;
    }
}
