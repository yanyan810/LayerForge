#include "ImageLoader.h"

#include <Windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <cctype>
#include <cwctype>
#include <format>

using Microsoft::WRL::ComPtr;

namespace {
std::string ToUtf8(const std::wstring& text) {
    if (text.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), size, nullptr, nullptr);
    return result;
}
}

bool ImageLoader::Load(const std::filesystem::path& path, ImageData& output, std::string& error) const {
    output = {};
    error.clear();

    std::wstring extension = path.extension().wstring();
    std::ranges::transform(extension, extension.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    if (extension != L".png" && extension != L".jpg" && extension != L".jpeg" && extension != L".webp") {
        error = "PNG, JPEG, or WebP files are supported.";
        return false;
    }

    ComPtr<IWICImagingFactory2> factory;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory2, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (FAILED(hr)) {
        error = std::format("Could not initialize Windows Imaging Component (0x{:08X}).", static_cast<unsigned>(hr));
        return false;
    }

    ComPtr<IWICBitmapDecoder> decoder;
    hr = factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder);
    if (FAILED(hr)) {
        error = std::format("Could not read the selected image (0x{:08X}).", static_cast<unsigned>(hr));
        return false;
    }

    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICFormatConverter> converter;
    hr = decoder->GetFrame(0, &frame);
    if (SUCCEEDED(hr)) hr = factory->CreateFormatConverter(&converter);
    if (SUCCEEDED(hr)) hr = converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);

    UINT width = 0, height = 0;
    if (SUCCEEDED(hr)) hr = converter->GetSize(&width, &height);
    if (FAILED(hr) || width == 0 || height == 0 || width > 32768 || height > 32768) {
        error = "The image format or dimensions are not supported.";
        return false;
    }

    std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4);
    const UINT stride = width * 4;
    hr = converter->CopyPixels(nullptr, stride, static_cast<UINT>(pixels.size()), pixels.data());
    if (FAILED(hr)) {
        error = std::format("Could not decode the selected image (0x{:08X}).", static_cast<unsigned>(hr));
        return false;
    }

    output.path = path;
    output.fileNameUtf8 = ToUtf8(path.filename().wstring());
    output.width = width;
    output.height = height;
    output.rgbaPixels = std::move(pixels);
    return true;
}
