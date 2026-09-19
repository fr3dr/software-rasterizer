#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <wwl.h>

#define TARGET_FPS 60
#define WIDTH 320
#define HEIGHT 240
#define SCREEN_SCALE 3
#define ASPECT ((float)WIDTH/HEIGHT)
#define FOV 60

#define MOVE_SPEED 3

#define FAR_CLIPPING_PLANE 20
#define NEAR_CLIPPING_PLANE 0.1
#define FACE_INDICES 16

#define DEG_2_RAD(DEG) (((DEG) / 180.0) * M_PI)

struct wwl_state *state = NULL;
double z_buffer[WIDTH * HEIGHT] = {0};

typedef struct {
    double x;
    double y;
    double z;
} vec3;

typedef struct {
    double pitch;
    double yaw;
    double roll;
} rotation;

typedef struct {
    vec3 pos;
    rotation rot;
} camera;

typedef struct {
    int indices[FACE_INDICES];
    size_t count;
    uint32_t color;
} face;

// TODO: support for face normals
typedef struct {
    vec3 *vertices;
    size_t vertices_len;
    vec3 *vertex_normals;
    size_t vertex_normals_len;
    face *faces;
    size_t faces_len;
    vec3 pos;
    rotation rot;
} model;

typedef struct {
    vec3 pos;
    double r;
    double g;
    double b;
} light;

light global_lights[] = {
    {.pos = {-4, 4, 4}, .r = 1.0, .g = 0.98, .b = 0.93},
    {.pos = {4, 10, 4}, .r = 1.0, .g = 0, .b = 1.0},
};

int parse_obj(const char *filename, vec3 **vertices, size_t *vertices_len, vec3 **vertex_normals, size_t *vertex_normals_len, face **faces, size_t *faces_len) {
    size_t vertices_size = 128;
    size_t vertices_pos = 0;
    *vertices = malloc(vertices_size * sizeof(vec3));

    size_t vertex_normals_size = 128;
    size_t vertex_normals_pos = 0;
    *vertex_normals = malloc(vertex_normals_size * sizeof(vec3));

    size_t faces_size = 128;
    size_t faces_pos = 0;
    *faces = malloc(vertices_size * sizeof(face));

    FILE *file = fopen(filename, "r");
    char *line = NULL;
    size_t line_len = 0;
    while (getline(&line, &line_len, file) != -1) {
        if (line_len == 0 || (line[0] != 'v' && line[0] != 'f')) {
            continue;
        }

        if (line[0] == 'v') {
            char *end_ptr;
            double x = strtof(&line[2], &end_ptr);
            double y = strtof(end_ptr + sizeof(char), &end_ptr);
            double z = strtof(end_ptr + sizeof(char), &end_ptr);

            if (line[1] == ' ') {
                if (vertices_pos >= vertices_size) {
                    vertices_size *= 2;
                    *vertices = realloc(*vertices, vertices_size * sizeof(vec3));
                }
                (*vertices)[vertices_pos] = (vec3){x, y, z};
                vertices_pos++;
            } else if (line[1] == 'n') {
                if (vertex_normals_pos >= vertex_normals_size) {
                    vertex_normals_size *= 2;
                    *vertex_normals = realloc(*vertex_normals, vertex_normals_size * sizeof(vec3));
                }
                (*vertex_normals)[vertex_normals_pos] = (vec3){x, y, z};
                vertex_normals_pos++;
            }
        } else if (line[0] == 'f') {
            if (faces_pos >= faces_size) {
                faces_size *= 2;
                *faces = realloc(*faces, faces_size * sizeof(face));
            }

            int index = 0;
            char *str = strtok(&line[2], " ");

            while (str != NULL && index < FACE_INDICES) {
                (*faces)[faces_pos].indices[index] = strtol(str, NULL, 10) - 1;
                index++;

                str = strtok(NULL, " ");
            }

            // model needs to have triangle faces
            assert(index == 3);

            // (*faces)[faces_pos].color = 0xFF000000 | (random() % 0x00FFFFFF);
            (*faces)[faces_pos].color = 0xFFFFFFFF;
            (*faces)[faces_pos].count = index;
            faces_pos++;
        }
    }

    free(line);

    *vertices_len = vertices_pos;
    *vertex_normals_len = vertex_normals_pos;
    printf("vertices count: %zu, vertex normals count: %zu\n", *vertices_len, *vertex_normals_len);
    // assert((*vertices_len) == (*vertex_normals_len));
    *faces_len = faces_pos;

    return 0;
}


/// vec3 util functions
static inline vec3 vec3_norm(vec3 vec) {
    double mag = sqrt(vec.x * vec.x + vec.y * vec.y + vec.z * vec.z);
    return (vec3){vec.x / mag, vec.y / mag, vec.z / mag};
}


/// transformation functions
static inline vec3 transform(vec3 pos, vec3 transformation) {
    return (vec3){pos.x += transformation.x, pos.y += transformation.y, pos.z += transformation.z};
}

vec3 rotate(vec3 pos, rotation rot) {
    vec3 ret = {0};

    double c_pitch = cos(rot.pitch);
    double c_yaw = cos(rot.yaw);
    double c_roll = cos(rot.roll);

    double s_pitch = sin(rot.pitch);
    double s_yaw = sin(rot.yaw);
    double s_roll = sin(rot.roll);

    ret.x = (pos.x * c_yaw * c_roll) + (pos.y * -s_yaw * s_roll) + (pos.z * s_yaw);
    ret.y = (pos.x * (c_pitch * s_roll + s_pitch * s_yaw * c_roll)) + (pos.y * (c_pitch * c_roll - s_pitch * s_yaw * s_roll)) + (pos.z * -s_pitch * c_yaw);
    ret.z = (pos.x * (s_pitch * s_roll - c_pitch * s_yaw * c_roll)) + (pos.y * (s_pitch * c_roll + c_pitch * s_yaw * s_roll)) + (pos.z * c_pitch * c_yaw);

    return ret;
}

static inline vec3 project(vec3 pos) {
    vec3 ret = {0};
    ret.x = pos.x / ASPECT / pos.z / tan(DEG_2_RAD(FOV) / 2.0);
    ret.y = pos.y / pos.z / tan(DEG_2_RAD(FOV) / 2.0);
    ret.z = pos.z;
    return ret;
}

static inline vec3 screen(vec3 pos) {
    vec3 ret = {0};
    ret.x = (pos.x + 1.0) / 2.0 * WIDTH;
    ret.y = (pos.y * -1.0 + 1.0) / 2.0 * HEIGHT;
    ret.z = pos.z;
    return ret;
}

vec3 apply_camera(camera camera, vec3 vertex) {
    vec3 transformation = {-camera.pos.x, -camera.pos.y, -camera.pos.z};
    rotation rotation = {-camera.rot.pitch, -camera.rot.yaw, -camera.rot.roll};

    return rotate(transform(vertex, transformation), rotation);
}


static inline vec3 clip_line(vec3 behind, vec3 infront) {
    double x_dist = infront.x - behind.x;
    double y_dist = infront.y - behind.y;
    double z_dist = infront.z - behind.z;
    double behind_dist = NEAR_CLIPPING_PLANE - behind.z;
    double percent = (behind_dist) / (z_dist);

    return (vec3){behind.x + x_dist * percent, behind.y + y_dist * percent, behind.z + z_dist * percent};
}

static inline double signed_triangle_area(vec3 a, vec3 b, vec3 c) {
    return 0.5 * ((a.x - c.x) * (b.y - a.y) - (a.x - b.x) * (c.y - a.y));
}

int clip_triangle(vec3 *v1r, vec3 *v2r, vec3 *v3r, vec3 *nv1, vec3 *nv2, vec3 *nv3) {
    vec3 v1 = *v1r;
    vec3 v2 = *v2r;
    vec3 v3 = *v3r;

    vec3 *clipped[3];
    int clipped_count = 0;

    vec3 *unclipped[3];
    int unclipped_count = 0;

    if (v1.z < NEAR_CLIPPING_PLANE) {
        clipped[clipped_count] = v1r;
        clipped_count++;
    } else {
        unclipped[unclipped_count] = v1r;
        unclipped_count++;
    }

    if (v2.z < NEAR_CLIPPING_PLANE) {
        clipped[clipped_count] = v2r;
        clipped_count++;
    } else {
        unclipped[unclipped_count] = v2r;
        unclipped_count++;
    }

    if (v3.z < NEAR_CLIPPING_PLANE) {
        clipped[clipped_count] = v3r;
        clipped_count++;
    } else {
        unclipped[unclipped_count] = v3r;
        unclipped_count++;
    }

    switch (clipped_count) {
    case 1: {
        vec3 pos1 = clip_line(*clipped[0], *unclipped[0]);
        vec3 pos2 = clip_line(*clipped[0], *unclipped[1]);
        *clipped[0] = pos1;

        *nv1 = pos1;
        *nv2 = pos2;
        *nv3 = *unclipped[1];
        return 1;
        break;
    }
    case 2:
        *clipped[0] = clip_line(*clipped[0], *unclipped[0]);
        *clipped[1] = clip_line(*clipped[1], *unclipped[0]);
        return 2;
        break;
    case 3:
        return 3;
        break;
    default:
        break;
    }

    return 0;
}

void draw_triangle(vec3 v1, vec3 v2, vec3 v3, vec3 vn1, vec3 vn2, vec3 vn3, vec3 v1w, vec3 v2w, vec3 v3w, uint32_t color) {
    double x_min = fmin(fmin(v1.x, v2.x), v3.x);
    double x_max = fmax(fmax(v1.x, v2.x), v3.x);
    double y_min = fmin(fmin(v1.y, v2.y), v3.y);
    double y_max = fmax(fmax(v1.y, v2.y), v3.y);

    if ((x_min < 0 && x_max < 0) || (x_min >= WIDTH && x_max >= WIDTH) || (y_min < 0 && y_max < 0) || (y_min >= HEIGHT && y_max >= HEIGHT)) {
        return;
    }

    double face_area = signed_triangle_area(v1, v2, v3);

    for (int screen_y = y_min; screen_y < y_max; screen_y++) {
        if (screen_y < 0)       continue;
        if (screen_y >= HEIGHT) break;
        for (int screen_x = x_min; screen_x < x_max; screen_x++) {
            if (screen_x < 0)      continue;
            if (screen_x >= WIDTH) break;

            // get barycentric coordinates
            vec3 point = {screen_x, screen_y, 1};
            double alpha = signed_triangle_area(point, v2, v3) / face_area;
            double beta = signed_triangle_area(point, v3, v1) / face_area;
            double gamma = signed_triangle_area(point, v1, v2) / face_area;
            if (alpha < 0 || beta < 0 || gamma < 0) {
                continue;
            }

            // z-buffering
            double z = 1 / (1 / v1.z * alpha + 1 / v2.z * beta + 1 / v3.z * gamma);
            int index = screen_y * WIDTH + screen_x;
            if (z >= z_buffer[index]) {
                continue;
            }
            z_buffer[index] = z;

            vec3 normal;
            normal.x = vn1.x * alpha + vn2.x * beta + vn3.x * gamma;
            normal.y = vn1.y * alpha + vn2.y * beta + vn3.y * gamma;
            normal.z = vn1.z * alpha + vn2.z * beta + vn3.z * gamma;
            normal = vec3_norm(normal);

            // lighting
            // TODO: optimize lighting calculations
            vec3 frag_pos;
            frag_pos.x = v1w.x * alpha + v2w.x * beta + v3w.x * gamma;
            frag_pos.y = v1w.y * alpha + v2w.y * beta + v3w.y * gamma;
            frag_pos.z = v1w.z * alpha + v2w.z * beta + v3w.z * gamma;
            double light_r = 0;
            double light_g = 0;
            double light_b = 0;
            int light_count = sizeof(global_lights) / sizeof(global_lights[0]);
            for (int i = 0; i < light_count; i++) {
                vec3 light_pos = global_lights[i].pos;
                vec3 light_dir = vec3_norm((vec3){light_pos.x - frag_pos.x, light_pos.y - frag_pos.y, light_pos.z - frag_pos.z});
                double light_dot = light_dir.x * normal.x + light_dir.y * normal.y + light_dir.z * normal.z;
                double ambient_light = 0.1;
                double diffuse = light_dot > 0 ? light_dot : 0;
                double light_intensity = (ambient_light + diffuse) <= 1 ? (ambient_light + diffuse) : 1;
                light_r += global_lights[i].r * light_intensity;
                light_g += global_lights[i].g * light_intensity;
                light_b += global_lights[i].b * light_intensity;
            }
            if (light_r > 1) light_r = 1;
            if (light_g > 1) light_g = 1;
            if (light_b > 1) light_b = 1;

            uint8_t r = (uint8_t)(color >> 16) * light_r;
            uint8_t g = (uint8_t)(color >> 8) * light_g;
            uint8_t b = (uint8_t)(color) * light_b;
            uint32_t pixel_color = 0xFF000000 | r | (g << 8) | (b << 16);
            wwl_draw_rect(state, screen_x * SCREEN_SCALE, screen_y * SCREEN_SCALE, SCREEN_SCALE, SCREEN_SCALE, pixel_color);
        }
    }
}


/// model functions
model create_model_from_obj(const char *filename) {
    model model = {
        .vertices = NULL,
        .vertices_len = 0,
        .vertex_normals = NULL,
        .vertex_normals_len = 0,
        .faces = NULL,
        .faces_len = 0,
        .pos = {0, 0, 0},
        .rot = {0, 0, 0},
    };

    parse_obj(filename, &model.vertices, &model.vertices_len, &model.vertex_normals, &model.vertex_normals_len, &model.faces, &model.faces_len);

    return model;
}

void free_model(model model) {
    if (model.vertices != NULL ) {
        free(model.vertices);
    }
    if (model.faces != NULL) {
        free(model.faces);
    }
}

void render_model(model model, camera camera) {
    for (size_t i = 0; i < model.faces_len; i++) {
        face face = model.faces[i];

        // TODO: frustum culling

        // get vertex normals
        vec3 vn1 = rotate(model.vertex_normals[face.indices[0]], model.rot);
        vec3 vn2 = rotate(model.vertex_normals[face.indices[1]], model.rot);
        vec3 vn3 = rotate(model.vertex_normals[face.indices[2]], model.rot);

        // get vertex world positions
        vec3 v1w = transform(rotate(model.vertices[face.indices[0]], model.rot), model.pos);
        vec3 v2w = transform(rotate(model.vertices[face.indices[1]], model.rot), model.pos);
        vec3 v3w = transform(rotate(model.vertices[face.indices[2]], model.rot), model.pos);

        // get vertex view space positions
        vec3 v1v = apply_camera(camera, v1w);
        vec3 v2v = apply_camera(camera, v2w);
        vec3 v3v = apply_camera(camera, v3w);

        // get the clipped vertices
        // TODO: also get the new triangles vertex normals
        vec3 nv1 = {0};
        vec3 nv2 = {0};
        vec3 nv3 = {0};
        int clipped = clip_triangle(&v1v, &v2v, &v3v, &nv1, &nv2, &nv3);
        if (clipped == 3) {
            continue;
        };

        // get vertex screen space positions
        vec3 v1s = screen(project(v1v));
        vec3 v2s = screen(project(v2v));
        vec3 v3s = screen(project(v3v));

        // backface culling
        vec3 v1v2 = {v2s.x - v1s.x, v2s.y - v1s.y, v2s.z - v1s.z};
        vec3 v1v3 = {v3s.x - v1s.x, v3s.y - v1s.y, v3s.z - v1s.z};
        vec3 normal = {0};
        normal.x = (v1v2.y * v1v3.z - v1v2.z * v1v3.y) / 3.0;
        normal.y = (v1v2.z * v1v3.x - v1v2.x * v1v3.z) / 3.0;
        normal.z = (v1v2.x * v1v3.y - v1v2.y * v1v3.x) / 3.0;
        if (normal.z < 0) {
            continue;
        }

        // draw clipped triangle
        if (clipped == 1) {
            vec3 v1 = screen(project(nv1));
            vec3 v2 = screen(project(nv2));
            vec3 v3 = screen(project(nv3));
            draw_triangle(v1, v2, v3, vn1, vn2, vn3, v1w, v2w, v3w, face.color);
        }

        draw_triangle(v1s, v2s, v3s, vn1, vn2, vn3, v1w, v2w, v3w, face.color);
    }
}


int main(void) {
    state = wwl_init(WIDTH * SCREEN_SCALE, HEIGHT * SCREEN_SCALE, "software rasterizer");
    wwl_set_fps(state, TARGET_FPS);
    wwl_set_min_size(state, WIDTH, HEIGHT);
    wwl_set_max_size(state, WIDTH, HEIGHT);

    bool mouse_control = false;
    double light_angle = 0;

    camera camera = {
        .pos = {0, 0, 0},
        .rot = {0, 0, 0},
    };

    model teapot = create_model_from_obj("models/utah_teapot.obj");
    teapot.pos = (vec3){0, -1, 5};

    model monkey = create_model_from_obj("models/monkey.obj");
    monkey.pos = (vec3){3, 0, 2};
    monkey.rot = (rotation){0, 1, 0};

    model cube = create_model_from_obj("models/cube.obj");
    cube.pos = (vec3){0, 0, 5};

    model tri = create_model_from_obj("models/tri.obj");
    tri.pos = (vec3){0, 0, 0};

    while (wwl_update(state)) {
        double dt = wwl_get_deltatime(state);
        printf("fps: %f, frametime: %f\n", 1 / dt, dt);

        light_angle += dt;
        global_lights[0].pos.x = cos(light_angle) * 3.0;
        global_lights[0].pos.z = 5 + sin(light_angle) * 3.0;

        if (wwl_is_key_down(state, KEY_SPACE)) {
            camera.pos.y += MOVE_SPEED * dt;
        }
        if (wwl_is_key_down(state, KEY_LEFTSHIFT)) {
            camera.pos.y -= MOVE_SPEED * dt;
        }
        if (wwl_is_key_down(state, KEY_A)) {
            camera.pos.x -= cos(camera.rot.yaw) * MOVE_SPEED * dt;
            camera.pos.z += sin(camera.rot.yaw) * MOVE_SPEED * dt;
        }
        if (wwl_is_key_down(state, KEY_D)) {
            camera.pos.x += cos(camera.rot.yaw) * MOVE_SPEED * dt;
            camera.pos.z -= sin(camera.rot.yaw) * MOVE_SPEED * dt;
        }
        if (wwl_is_key_down(state, KEY_W)) {
            camera.pos.x += sin(camera.rot.yaw) * MOVE_SPEED * dt;
            camera.pos.z += cos(camera.rot.yaw) * MOVE_SPEED * dt;
        }
        if (wwl_is_key_down(state, KEY_S)) {
            camera.pos.x -= sin(camera.rot.yaw) * MOVE_SPEED * dt;
            camera.pos.z -= cos(camera.rot.yaw) * MOVE_SPEED * dt;
        }

        if (wwl_is_key_down(state, KEY_RIGHT)) {
            cube.pos.x += dt;
        }
        if (wwl_is_key_down(state, KEY_LEFT)) {
            cube.pos.x -= dt;
        }
        if (wwl_is_key_down(state, KEY_UP)) {
            cube.pos.z += dt;
        }
        if (wwl_is_key_down(state, KEY_DOWN)) {
            cube.pos.z -= dt;
        }

        if (wwl_is_button_pressed(state, MOUSE_BTN_LEFT)) {
            wwl_lock_cursor(state);
            wwl_set_cursor(state, NULL);
            mouse_control = true;
        }
        if (wwl_is_key_pressed(state, KEY_ESC)) {
            wwl_unlock_cursor(state);
            wwl_set_cursor(state, "default");
            mouse_control = false;
        }

        if (mouse_control) {
            camera.rot.yaw += wwl_get_mouse_motion_x(state) / 200.0;
            camera.rot.pitch += wwl_get_mouse_motion_y(state) / 200.0;
            if (camera.rot.pitch > M_PI/2) {
                camera.rot.pitch = M_PI/2;
            }
            if (camera.rot.pitch < -M_PI/2) {
                camera.rot.pitch = -M_PI/2;
            }
        }

        wwl_clear_background(state, 0xFF000000);
        for (int i = 0; i < WIDTH * HEIGHT; i++) {
            z_buffer[i] = FAR_CLIPPING_PLANE;
        }

        render_model(teapot, camera);
        render_model(monkey, camera);
        render_model(cube, camera);
        render_model(tri, camera);

        wwl_update_end(state);
    }

    free_model(teapot);
    free_model(monkey);
    free_model(cube);
    free_model(tri);
    wwl_close(state);
}
