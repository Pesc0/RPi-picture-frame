#pragma once

#include <string>
#include <vector>


class ImageLoader {
public:
    ImageLoader(const std::string& path);
    ~ImageLoader();
    bool init_is_successful() { return init_success; }

    bool load_file_list();

    bool load_next_image();
    bool load_prev_image();
    bool new_image_has_been_loaded() { return new_image_loaded; }

    void switch_active_texture();
    float correct_fade_direction(float fade) { return current_active_texture ? (1 - fade) : fade; }

private:
    int get_file_idx(const std::string &path);
    bool load_image_to_back_texture(int file_idx);

private:
    bool init_success;
    const std::string folder_path;
    std::vector<std::string> img_files;

    bool current_active_texture = 0; // 0 = tex0, 1 = tex1
    std::string tex_loaded_filenames[2]; //currently loaded filename for tex0 and tex1

    bool new_image_loaded = false;
};