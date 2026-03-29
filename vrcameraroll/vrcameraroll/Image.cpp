#pragma execution_character_set("utf-8")
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include "Image.h"
#include <cstdio>

bool Image::LoadFromFile(const std::filesystem::path& file_path) {
    int channels;
    // RGBA の 4ch で強制デコード
    uint8_t* data = stbi_load(file_path.string().c_str(), &width, &height, &channels, 4);
    if (!data) {
        printf("Image load failed: %s\n", file_path.string().c_str());
        return false;
    }

    pixels.assign(data, data + width * height * 4);
    stbi_image_free(data);

    path      = file_path;
    timestamp = std::filesystem::last_write_time(file_path);
    return true;
}
