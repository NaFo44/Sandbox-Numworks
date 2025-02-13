#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <optional>
#include "eadkpp.h"

extern const char eadk_app_name[] __attribute__((section(".rodata.eadk_app_name"))) = "Sandbox";
extern const uint32_t eadk_api_level __attribute__((section(".rodata.eadk_api_level"))) = 0;

// Définition des couleurs
constexpr EADK::Color black(0x000000);
constexpr EADK::Color white(0xFFFFFF);
constexpr EADK::Color red(0x9B0000);
constexpr EADK::Color green(0x00FF00);
constexpr EADK::Color grey(0x808080);
constexpr EADK::Color sand(0xffe0a0);
constexpr EADK::Color water(0x0000FF);

constexpr size_t SCREEN_WIDTH = 320;
constexpr size_t SCREEN_HEIGHT = 240;

// Fonction pour dessiner une ligne épaisse
void pushThickLine(int x0, int y0, int x1, int y1, EADK::Color color, int thickness) {
    int dx = abs(x1 - x0);
    int dy = abs(y1 - y0);
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;

    while (true) {
        for (int i = -thickness / 2; i <= thickness / 2; ++i) {
            for (int j = -thickness / 2; j <= thickness / 2; ++j) {
                EADK::Display::pushRectUniform(EADK::Rect(x0 + i, y0 + j, 1, 1), color);
            }
        }
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y0 += sy;
        }
    }
}

// Structure pour représenter un point
struct Point {
    size_t x, y;

    Point(size_t x, size_t y) : x(x), y(y) {}

    Point operator+(const Point& other) const {
        return Point(x + other.x, y + other.y);
    }

    bool operator==(const Point& other) const {
        return x == other.x && y == other.y;
    }

    bool operator!=(const Point& other) const {
        return !(*this == other);
    }
};

// Structure pour représenter un rectangle
struct Rect {
    Point loc, dims;

    Rect(Point loc, Point dims) : loc(loc), dims(dims) {}
    Rect(size_t x, size_t y, size_t w, size_t h) : Rect(Point(x, y), Point(w, h)) {}
};

// Structure pour représenter la carte de particules
struct ParticleMap {
    enum CellType {
        EMPTY = 0,
        OBSTACLE = 1,
        PARTICLE_HEAVY = 2,  // Sable
        PARTICLE_LIGHT = 3,  // Fumée
        PARTICLE_WATER = 4,  // Eau
    };

    ParticleMap(Point dims, CellType ty = ParticleMap::EMPTY)
        : dims(dims), buf_size((dims.x * dims.y + 1) >> 1) {
        buf = new uint8_t[buf_size];
        if (!buf) abort();
        fill(ty);
    }

    ~ParticleMap() {
        delete[] buf;
    }

    void fill(CellType ty) {
        uint8_t elem = ((ty << 0) | (ty << 4) | (ty << 8) | (ty << 12));
        std::fill_n(buf, buf_size, elem);
    }

    void clear() {
        fill(ParticleMap::EMPTY);
    }

    bool check_loc(const Point& loc) const {
        return loc.x < dims.x && loc.y < dims.y;
    }

    void assert_loc(const Point& loc) const {
        if (!check_loc(loc)) abort();
    }

    CellType get(const Point& loc) const {
        assert_loc(loc);
        auto elem_idx = loc.y * dims.x + loc.x;
        auto idx = elem_idx >> 1;
        auto off = (elem_idx & 1) << 2;
        return static_cast<CellType>((buf[idx] >> off) & 15);
    }

    void set(const Point& loc, CellType ty) const {
        assert_loc(loc);
        auto elem_idx = loc.y * dims.x + loc.x;
        auto idx = elem_idx >> 1;
        auto off = (elem_idx & 1) << 2;
        buf[idx] = (buf[idx] & ~(15 << off)) | (ty << off);
    }

    void swap(ParticleMap& other) {
        if (buf_size != other.buf_size) abort();
        std::swap(buf, other.buf);
    }

    Point dims;
    size_t buf_size;
    uint8_t* buf;
};

// Structure pour représenter gérer l'état des particules
struct FluxState {
    static constexpr EADK::Color particle_colors[] = {
        black, grey, sand, red, water
    };

    FluxState(Point dims) : back(ParticleMap(dims)), front(ParticleMap(dims)) {}

    Point dims() const {
        return back.dims;
    }

    ParticleMap::CellType get_particle(const Point& loc) const {
        return front.get(loc);
    }

    bool is_obstacle(const Point& loc) const {
        return get_particle(loc) == ParticleMap::OBSTACLE;
    }

    bool is_fully_empty(const Point& loc) {
        return back.get(loc) == ParticleMap::EMPTY && front.get(loc) == ParticleMap::EMPTY;
    }

    void set_particle(const Point& loc, ParticleMap::CellType type) {
        front.set(loc, type);
    }

    void set_particle_rect(const Rect& area, ParticleMap::CellType type) {
        const auto lim = area.loc + area.dims;
        for (auto y = area.loc.y; y < lim.y; ++y)
            for (auto x = area.loc.x; x < lim.x; ++x)
                front.set(Point(x, y), type);
    }

    void set_obstacle(const Rect& area) {
        set_particle_rect(area, ParticleMap::OBSTACLE);
    }

    ParticleMap::CellType clear_particle(const Point& loc) {
        auto ty = front.get(loc);
        set_particle(loc, ParticleMap::EMPTY);
        return ty;
    }

    void move_particle(const Point& src, const Point& dst) {
        set_particle(dst, clear_particle(src));
    }

    void back_set_particle(const Point& loc, ParticleMap::CellType ty) {
        back.set(loc, ty);
    }

    void flip() {
        back.swap(front);
    }

    Point left(const Point& loc) {
        return Point(loc.x - 1, loc.y);
    }

    Point right(const Point& loc) {
        return Point(loc.x + 1, loc.y);
    }

    Point above(const Point& loc) {
        return Point(loc.x, loc.y - 1);
    }

    Point below(const Point& loc) {
        return Point(loc.x, loc.y + 1);
    }

    ParticleMap back;
    ParticleMap front;
};

// Structure pour représenter le curseur
struct Cursor {
    Point position;
    bool selecting;
    std::optional<Point> start_point;

    Cursor() : position(50, 50), selecting(false), start_point(std::nullopt) {}

    void move(int dx, int dy) {
        position.x += dx;
        position.y += dy;
    }

    void start_selection() {
        selecting = true;
        start_point = position;
    }

    void end_selection() {
        selecting = false;
        start_point = std::nullopt;
    }

    std::optional<Point> get_end_point() const {
        if (selecting && start_point) {
            return position;
        }
        return std::nullopt;
    }
};

// Fonction pour mettre à jour le monde
void update_world(FluxState& state) {
    auto dims = state.dims();

    for (auto y = 0; y < dims.y; ++y) {
        for (auto x = 0; x < dims.x; ++x) {
            auto loc = Point(x, y);
            auto ty = state.get_particle(loc);
            if (ty != ParticleMap::OBSTACLE) ty = ParticleMap::EMPTY;
            state.back_set_particle(loc, ty);
        }
    }

    for (auto y = 0; y < dims.y; ++y) {
        for (auto x = 0; x < dims.x; ++x) {
            auto loc = Point(x, y);
            auto ty = state.get_particle(loc);
            auto new_loc = loc;

            if (ty == ParticleMap::EMPTY || ty == ParticleMap::OBSTACLE) continue;

            int leftright = rand() % 5 - 2;

            if (ty == ParticleMap::PARTICLE_HEAVY) {
                auto below_loc = state.below(new_loc);
                auto left_below = state.left(below_loc);
                auto right_below = state.right(below_loc);

                if (state.is_fully_empty(below_loc)) {
                    new_loc = below_loc;
                } else {
                    int direction = rand() % 2 == 0 ? -1 : 1;
                    if (direction == -1 && state.is_fully_empty(left_below))
                        new_loc = left_below;
                    else if (direction == 1 && state.is_fully_empty(right_below))
                        new_loc = right_below;
                }
            } else if (ty == ParticleMap::PARTICLE_LIGHT) {
                auto above_loc = state.above(new_loc);
                auto left_above = state.left(above_loc);
                auto right_above = state.right(above_loc);

                if (state.is_fully_empty(above_loc)) {
                    new_loc = above_loc;
                } else {
                    int direction = rand() % 2 == 0 ? -1 : 1;
                    if (direction == -1 && state.is_fully_empty(left_above))
                        new_loc = left_above;
                    else if (direction == 1 && state.is_fully_empty(right_above))
                        new_loc = right_above;
                }
            } else if (ty == ParticleMap::PARTICLE_WATER) {
                auto below_loc = state.below(new_loc);
                auto left_below = state.left(below_loc);
                auto right_below = state.right(below_loc);
                auto left = state.left(new_loc);
                auto right = state.right(new_loc);

                if (state.is_fully_empty(below_loc)) {
                    new_loc = below_loc;
                } else if (state.is_fully_empty(left_below)) {
                    new_loc = left_below;
                } else if (state.is_fully_empty(right_below)) {
                    new_loc = right_below;
                } else if (state.is_fully_empty(left)) {
                    new_loc = left;
                } else if (state.is_fully_empty(right)) {
                    new_loc = right;
                }
            }

            if (leftright == -1) {
                auto left_loc = state.left(new_loc);
                if (state.is_fully_empty(left_loc)) {
                    new_loc = left_loc;
                }
            } else if (leftright == 1) {
                auto right_loc = state.right(new_loc);
                if (state.is_fully_empty(right_loc)) {
                    new_loc = right_loc;
                }
            }

            // Vérification des limites pour supprimer les particules hors écran
            if (new_loc.x < 3 || new_loc.x > dims.x - 3 || new_loc.y < 3 || new_loc.y >= dims.y - 3) {
                state.back_set_particle(loc, ParticleMap::EMPTY);
            } else {
                state.back_set_particle(new_loc, ty);
            }
        }
    }

    state.flip();
}

// Fonction pour dessiner de monde
void render(const FluxState& state) {
    auto dims = state.dims();
    EADK::Display::pushRectUniform(EADK::Screen::Rect, black);

    for (int y = 0; y < dims.y; ++y) {
        for (int x = 0; x < dims.x; ++x) {
            auto ty = state.get_particle(Point(x, y));
            if (ty == ParticleMap::EMPTY) continue;
            EADK::Display::pushRectUniform(EADK::Rect(x, y, 1, 1), FluxState::particle_colors[ty]);
        }
    }
}

// Fonction pour attendre l'appui de la touche
void wait() {
    while (true) {
        EADK::Keyboard::State keyboardState = EADK::Keyboard::scan();
        if (keyboardState.keyDown(EADK::Keyboard::Key::OK)) {
            return;
        }
        if (keyboardState.keyDown(EADK::Keyboard::Key::Home)) {
            return;
        }
        EADK::Timing::msleep(20);
    }
}

// Fonction pour attendre le relâchement de la touche
void waitForRelease() {
    while (true) {
        EADK::Keyboard::State keyboardState = EADK::Keyboard::scan();
        if (!keyboardState.keyDown(EADK::Keyboard::Key::OK)) {
            EADK::Timing::msleep(20);
            EADK::Keyboard::scan();
            return;
        }
        EADK::Timing::msleep(20);
    }
}

// Fonction principale
int main() {
    FluxState state(Point(EADK::Screen::Width, EADK::Screen::Height));
    Cursor cursor;
    bool editing_mode = false;

    int cursorwidth = 5;
    int cursorheight = 5;

    while (true) {
        render(state);
        EADK::Display::pushRectUniform(EADK::Rect(cursor.position.x-1, cursor.position.y-1, cursorwidth, cursorheight), white);

        if (editing_mode) {
            EADK::Display::pushRectUniform(EADK::Rect(cursor.position.x-1, cursor.position.y-1, cursorwidth, cursorheight), green);
            if (cursor.selecting && cursor.start_point) {
                auto end_point = cursor.get_end_point();
                if (end_point) {
                    pushThickLine(cursor.start_point->x, cursor.start_point->y, end_point->x, end_point->y, green, 3);
                }
            }
        }

        EADK::Keyboard::State keyboardState = EADK::Keyboard::scan();

        if (keyboardState.keyDown(EADK::Keyboard::Key::Up) && cursor.position.y > 3) {
            cursor.move(0, -3);
        }
        if (keyboardState.keyDown(EADK::Keyboard::Key::Down) && cursor.position.y < SCREEN_HEIGHT - 3 - cursorheight) {
            cursor.move(0, 3);
        }
        if (keyboardState.keyDown(EADK::Keyboard::Key::Left) && cursor.position.x > 3) {
            cursor.move(-3, 0);
        }
        if (keyboardState.keyDown(EADK::Keyboard::Key::Right) && cursor.position.x < SCREEN_WIDTH - 3 - cursorwidth) {
            cursor.move(3, 0);
        }
        if (keyboardState.keyDown(EADK::Keyboard::Key::OK)) {
            wait();
            waitForRelease();
            if (!editing_mode) {
                editing_mode = true;
                cursor.start_selection();
            } else if (cursor.selecting) {
                auto end_point = cursor.get_end_point();
                if (end_point) {
                    pushThickLine(cursor.start_point->x, cursor.start_point->y, end_point->x, end_point->y, red, 3);
                    int dx = abs(end_point->x - cursor.start_point->x);
                    int dy = abs(end_point->y - cursor.start_point->y);
                    int sx = cursor.start_point->x < end_point->x ? 1 : -1;
                    int sy = cursor.start_point->y < end_point->y ? 1 : -1;
                    int err = dx - dy;
                    int x0 = cursor.start_point->x;
                    int y0 = cursor.start_point->y;

                    while (true) {
                        for (int i = -1; i <= 1; ++i) {
                            for (int j = -1; j <= 1; ++j) {
                                state.set_particle(Point(x0 + i, y0 + j), ParticleMap::OBSTACLE);
                            }
                        }
                        if (x0 == end_point->x && y0 == end_point->y) break;
                        int e2 = 2 * err;
                        if (e2 > -dy) {
                            err -= dy;
                            x0 += sx;
                        }
                        if (e2 < dx) {
                            err += dx;
                            y0 += sy;
                        }
                    }
                }
                cursor.end_selection();
                editing_mode = false;
            }
        } else if (keyboardState.keyDown(EADK::Keyboard::Key::Back)) {
            state.set_particle_rect(Rect(cursor.position.x, cursor.position.y, cursorwidth, cursorheight), ParticleMap::PARTICLE_HEAVY);
        } else if (keyboardState.keyDown(EADK::Keyboard::Key::Backspace)) {
            state.set_particle_rect(Rect(cursor.position.x-1, cursor.position.y, cursorwidth, cursorheight), ParticleMap::EMPTY);
        } else if (keyboardState.keyDown(EADK::Keyboard::Key::EXE)) {
            state.set_particle_rect(Rect(cursor.position.x, cursor.position.y, cursorwidth, cursorheight), ParticleMap::PARTICLE_WATER);
        } else if (keyboardState.keyDown(EADK::Keyboard::Key::Toolbox)) {
            state.set_particle_rect(Rect(cursor.position.x, cursor.position.y, cursorwidth, cursorheight), ParticleMap::PARTICLE_LIGHT);
        } else if (keyboardState.keyDown(EADK::Keyboard::Key::Plus)) {
            if (cursorheight < 80) {
                cursorheight+=1;
                cursorwidth+=1;
                cursor.position.x-=1;
                cursor.position.y-=1;
            }
        } else if (keyboardState.keyDown(EADK::Keyboard::Key::Minus)) {
            if (cursorheight > 2) {
                cursorheight-=1;
                cursorwidth-=1;
                cursor.position.x+=1;
                cursor.position.y+=1;
            }
        }

        update_world(state);
    }

    return 0;
}

// Implémentations personnalisées pour les fonctions manquantes
void* operator new[](size_t size) {
    return malloc(size);
}

void operator delete[](void* ptr) {
    free(ptr);
}

void abort() {
    while (true) {}
}

void _exit(int status) {
    while (true) {}
}

int _kill(int pid, int sig) {
    return 0;
}

int _getpid() {
    return 0;
}
