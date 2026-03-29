#pragma once
#include <vector>
#include <filesystem>

// 1枚の画像データ。OpenVR の SetOverlayRaw に渡せる RGBA バッファを保持する。
struct Image {
    std::vector<uint8_t> pixels; // RGBA, width * height * 4 bytes
    int width  = 0;
    int height = 0;
    std::filesystem::path path;
    std::filesystem::file_time_type timestamp;

    bool IsLoaded() const { return !pixels.empty(); }

    // stb_image でファイルを読み込み、RGBA バッファに展開する。
    // 成功したら true を返す。
    bool LoadFromFile(const std::filesystem::path& file_path);

    void Unload() { pixels.clear(); width = 0; height = 0; }
};
