#include<GL/glut.h>
#include<stdio.h>
#include<stdlib.h>
#include<time.h>
#include<math.h>
#include<stdbool.h>

#define min(x, y) x > y ? y : x
#define max(x, y) x > y ? x : y

#define SCREEN_RATIO

typedef struct VectorInt {
    int x, y;
} VectorInt;

typedef struct Vector {
    GLsizei x, y;
} Vector;

typedef struct VectorFloat {
    float x, y;
} VectorFloat;

typedef struct Image {
    Vector size;
    GLubyte* image;
} Image;

typedef struct RGB {
    GLubyte r, g, b;
} RGB;

typedef struct Trail {
    unsigned long since;
    VectorFloat position;
    RGB color;
    float angle;
    struct Trail* next;
} Trail;

typedef struct Particle {
    VectorFloat position;
    VectorFloat linear_speed;
    RGB color;
    unsigned long die_at;
    float direction;
    float speed;
    Trail* trail;
    char active;
} Particle;

#define PS_LERP 1
#define PS_RAND 2
typedef struct ParticleSystem {
    Particle* current;
    unsigned long active;
    unsigned long count;
    unsigned long min_lifetime;
    unsigned long max_lifetime;
    unsigned long trail_lifetime;
    float start_size;
    RGB color1;
    RGB color2;
    struct ParticleSystem* next;
    float (*remap)(float);
} ParticleSystem;

typedef struct Rocket {
    float target_y;
    VectorFloat speed;
    VectorFloat position;
    ParticleSystem* sys;
    Trail* trail;
    struct Rocket* next;
} Rocket;

long long last_time;
VectorFloat mouse_pos = {0, 0};
float mouse_rot = 0;
ParticleSystem* first_sys = NULL;
Rocket* first_rocket = NULL;

long long __time_get_current_timestamp_internal(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * (long long) 10e5 + t.tv_nsec / (long long) 10e2;
}

RGB lerp_color(RGB color1, RGB color2, float lerp) {
    float out[3] = {
        color1.r + ((float) color2.r - color1.r) * lerp,
        color1.g + ((float) color2.g - color1.g) * lerp,
        color1.b + ((float) color2.b - color1.b) * lerp,
    };
    return (RGB) {out[0], out[1], out[2]};
}

VectorFloat oct_points[8];
void init_oct_points() {
    const float angle = 22.5f * M_PI / 180.0f;
    const float s = sinf(angle);
    const float c = cosf(angle);
    oct_points[0] = (VectorFloat) {s, c};
    oct_points[1] = (VectorFloat) {c, s};
    oct_points[2] = (VectorFloat) {c, -s};
    oct_points[3] = (VectorFloat) {s, -c};
    oct_points[4] = (VectorFloat) {-s, -c};
    oct_points[5] = (VectorFloat) {-c, -s};
    oct_points[6] = (VectorFloat) {-c, s};
    oct_points[7] = (VectorFloat) {-s, c};
}

VectorFloat point(VectorFloat pixel, Vector screen_size) {
    return (VectorFloat) {
        (pixel.x - (screen_size.x >> 1)) / screen_size.x * 2,
        (pixel.y - (screen_size.y >> 1)) / screen_size.y * 2
    };
}

void draw_oct(VectorFloat position, float radius, Vector screen_size, RGB color) {
    glColor3ub(color.r, color.g, color.b);
    glBegin(GL_POLYGON);
    for (int i = 0; i < 8; i++) {
        glVertex2f(
            (oct_points[i].x * radius + position.x - (screen_size.x >> 1)) / screen_size.x * 2,
            (oct_points[i].y * radius + position.y - (screen_size.y >> 1)) / screen_size.y * 2
        );
    }
    glEnd();
}

void update_trail(Trail** data, unsigned long width, unsigned long existence, unsigned long now, 
            VectorFloat alignment, float rotation, Vector dimension, RGB color, char emmit);

char update_particle(ParticleSystem* sys, Particle* current, unsigned long now, Vector dimensions, float deltatime) {
    if (current->die_at < now) return 1;
    current->position.x += sinf(current->direction) * current->speed * deltatime;
    current->position.y += cosf(current->direction) * current->speed * deltatime;
    current->linear_speed.y -= 200.0f * deltatime;
    current->position.x += current->linear_speed.x * deltatime;
    current->position.y += current->linear_speed.y * deltatime;
    if (current->position.x <= -dimensions.x || current->position.x >= dimensions.x) return 1;
    if (current->position.y <= -dimensions.y || current->position.y >= dimensions.y) return 1;
    RGB color = lerp_color(sys->color1, sys->color2, 1 - ((current->die_at - now) / (float) (sys->max_lifetime)));
    float size;
    if (sys->remap) {
        size = sys->start_size * sys->remap(((current->die_at - now) / (float) (sys->max_lifetime)));
    }
    else {
        size = sys->start_size * ((current->die_at - now) / (float) (sys->max_lifetime));
    }
    update_trail(&current->trail, size * 1.2, sys->trail_lifetime, now, current->position, current->direction + M_PI_2, dimensions, color, 1);
    draw_oct(current->position, size, dimensions, color);
    return 0;
}

void free_trail(Trail* trail) {
    while(trail) {
        Trail* next = trail->next;
        free(trail);
        trail = next;
    }
}

void update_system(ParticleSystem* sys, Vector dimensions, unsigned long now, float deltatime) {
    for (unsigned long i = 0; i < sys->count; i++) {
        if (sys->current[i].active) {
            if (update_particle(sys, sys->current + i, now, dimensions, deltatime)) {
                Trail* trail = sys->current[i].trail;
                free_trail(trail);
                sys->current[i].active = false;
                sys->active--;
            }
        }
    }
}

void __render_trail(Trail* trail, unsigned long width, unsigned long existence, unsigned long now, Vector dimension) {
    float factor = ((float) existence - (float) (now - trail->since)) / (float) existence;
    if (factor < 0) {
        factor = 0;
    }
    VectorFloat p = (VectorFloat) {sinf(trail->angle), cosf(trail->angle)};
    VectorFloat c1 = point((VectorFloat) {
        trail->position.x + p.x * width * factor,
        trail->position.y + p.y * width * factor
    }, dimension);
    VectorFloat c2 = point((VectorFloat) {
        trail->position.x - p.x * width * factor,
        trail->position.y - p.y * width * factor
    }, dimension);
    glColor3ub(trail->color.r, trail->color.g, trail->color.b);
    glVertex2f(c1.x, c1.y);
    glVertex2f(c2.x, c2.y);
    glVertex2f(c2.x, c2.y);
    glVertex2f(c1.x, c1.y);
    if (trail->next) __render_trail(trail->next, width, existence, now, dimension);
}

void init_particle(ParticleSystem* particle_system, int x, int y, int amount, unsigned long min_lifetime, 
        unsigned long max_lifetime, unsigned long trail_lifetime, RGB color1, RGB color2, float linear_speed, 
        float random_speed, float size, float (*remap)(float)) {
    particle_system->current = malloc(sizeof(Particle) * amount);
    particle_system->count = amount;
    particle_system->active = amount;
    particle_system->min_lifetime = min_lifetime;
    particle_system->max_lifetime = max_lifetime;
    particle_system->color1 = color1;
    particle_system->color2 = color2;
    particle_system->next = NULL;
    particle_system->start_size = size;
    particle_system->trail_lifetime = trail_lifetime;
    particle_system->remap = remap;
    for (unsigned long i = 0; i < particle_system->count; i++) {
        particle_system->current[i].position.x = x;
        particle_system->current[i].position.y = y;
        particle_system->current[i].die_at = last_time + particle_system->min_lifetime +
                ((float) random() / (float) RAND_MAX) * (particle_system->max_lifetime - particle_system->min_lifetime);
        particle_system->current[i].linear_speed.x = 0;
        particle_system->current[i].linear_speed.y = 0;
        particle_system->current[i].direction = ((float) random() / (float) RAND_MAX) * 2 * M_PI;
        particle_system->current[i].speed = ((float) random() / (float) RAND_MAX) * random_speed + linear_speed;
        particle_system->current[i].active = true;
        particle_system->current[i].trail = NULL;
    }
}

float remap_blue(float in) {
    in = in;
    return in > 0.75f ? 1 : 0;
}

void init_random_particle(ParticleSystem* sys, int x, int y) {
    switch (random() % 4) {
    case 0:
        init_particle(sys, x, y, 100, 200000, 500000, 100000, (RGB) {255, 255, 10},
                (RGB) {255, 0, 0}, 100, 900, 5, NULL);
        break;
    case 1:
        init_particle(sys, x, y, 100, 200000, 500000, 100000, (RGB) {100, 255, 120},
                (RGB) {50, 200, 60}, 100, 900, 7, NULL);
        break;
    case 2:
        init_particle(sys, x, y, 100, 500000, 1000000, 200000, (RGB) {255, 255, 255},
                (RGB) {200, 200, 200}, 100, 500, 2, NULL);
        break;
    case 3:
        init_particle(sys, x, y, 100, 800000, 2000000, 500000, (RGB) {0, 0, 255},
                (RGB) {255, 0, 0}, 100, 900, 1, remap_blue);
        break;
    }
}

char update_rocket(Rocket* rocket, Vector dimensions, float deltatime) {
    rocket->position.x += rocket->speed.x * deltatime;
    rocket->position.y += rocket->speed.y * deltatime;
    if (rocket->position.y >= rocket->target_y) {
        init_random_particle(rocket->sys, rocket->position.x, rocket->position.y);
        rocket->sys->next = first_sys;
        first_sys = rocket->sys;
        return 1;
    }
    update_trail(&rocket->trail, 2, 100000, last_time, rocket->position, 
            -atan2f(rocket->speed.y, rocket->speed.x), dimensions, (RGB) {255, 120, 0}, 1);
    return 0;
}

void update_trail(Trail** trail_location, unsigned long width, unsigned long existence, unsigned long now, 
            VectorFloat alignment, float rotation, Vector dimension, RGB color, char emmit) {
    Trail* first = *trail_location;
    if (emmit) {
        Trail* new_trail = malloc(sizeof(Trail));
        new_trail->next = *trail_location;
        new_trail->since = now;
        new_trail->position = alignment;
        new_trail->angle = rotation;
        new_trail->color = color;
        *trail_location = new_trail;
        first = new_trail;
    }
    
    Trail* trail = first;
    while(trail) {
        Trail* next = trail->next;
        Trail** next_data = &trail->next;
        float factor = (float) existence - (float) (now - trail->since);
        if (factor < 0 || !trail_location) {
            if (trail_location) {
                *trail_location = NULL;
                trail_location = NULL;
            }
            free(trail);
        }
        else {
            trail_location = next_data;
        }
        trail = next;
    }
    if (first) {
        glColor3ub(color.r, color.g, color.b);
        glBegin(GL_QUADS);
        VectorFloat c = point((VectorFloat) {
            alignment.x,
            alignment.y
        }, dimension);
        glVertex2f(c.x, c.y);
        glVertex2f(c.x, c.y);
        __render_trail(first, width, existence, now, dimension);
        glEnd();
    }
}

void make_rocket() {
    ParticleSystem* new_system = malloc(sizeof(ParticleSystem));
    Rocket* rocket = malloc(sizeof(Rocket));
    float angle = ((float) random() / (float) RAND_MAX) * M_PI / 4 - M_PI / 8;
    rocket->speed.x = sinf(angle) * 2000.0f;
    rocket->speed.y = cosf(angle) * 2000.0f;
    rocket->position.x = glutGet(GLUT_SCREEN_WIDTH) * (((float) random() / (float) RAND_MAX) * 0.6f + 0.2f);
    rocket->position.y = 0;
    float screen_height = glutGet(GLUT_SCREEN_HEIGHT);
    rocket->target_y = cosf(angle) * screen_height / 2 + screen_height * ((float) random() / (float) RAND_MAX) / 2;
    rocket->sys = new_system;
    rocket->trail = NULL;
    rocket->next = first_rocket;
    first_rocket = rocket;
}

float rockets_per_second = 1.5f;

void display() {
    long long now = __time_get_current_timestamp_internal();
    float deltatime = (now - last_time) / 10e5f;
    last_time = now;

    Vector dimensions;
    dimensions.x = glutGet(GLUT_WINDOW_WIDTH);
    dimensions.y = glutGet(GLUT_WINDOW_HEIGHT);
    glClearColor(0.0, 0.0, 0.0, 0.0);
    glClear(GL_COLOR_BUFFER_BIT);

    if ((float) random() < RAND_MAX * rockets_per_second * deltatime) {
        make_rocket();
    }

    Rocket* rocket = first_rocket;
    Rocket** prev_r = &first_rocket;
    while (rocket) {
        Rocket* next = rocket->next;
        if (update_rocket(rocket, dimensions, deltatime)) {
            *prev_r = next;
            free_trail(rocket->trail);
            free(rocket);
        }
        else {
            prev_r = &rocket->next;
        }
        rocket = next;
    }

    ParticleSystem* cursor = first_sys;
    ParticleSystem** prev_s = &first_sys;
    while (cursor) {
        update_system(cursor, dimensions, now, deltatime);
        if (cursor->active) {
            prev_s = &cursor->next;
            cursor = cursor->next;
        }
        else {
            ParticleSystem* next = cursor->next;
            *prev_s = next;
            free(cursor->current);
            free(cursor);
            cursor = next;
        }
    }

    glFlush();
}

void keyboard(unsigned char key, int x, int y) {
    if (key == '\e') {
        glutDestroyWindow(glutGetWindow());
    }
}

void mouse(int x, int y) {
    VectorFloat new = {
        x, glutGet(GLUT_WINDOW_HEIGHT) - y
    };
    mouse_rot = atan2f(mouse_pos.y - new.y, new.x - mouse_pos.x);
    mouse_pos.x = new.x;
    mouse_pos.y = new.y;
}

void mouse_event(int button, int state, int x, int y) {
    y = glutGet(GLUT_WINDOW_HEIGHT) - y;
    if (button == GLUT_LEFT_BUTTON && state == GLUT_DOWN) {
        make_rocket();
    }
}

ParticleSystem particle_system;

int main(int argc, char** argv) {
    srandom(__time_get_current_timestamp_internal());
    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_SINGLE | GLUT_RGB);
    glShadeModel(GL_FLAT);
    Vector size;
    #ifdef SCREEN_RATIO
        size = (Vector) {glutGet(GLUT_SCREEN_WIDTH), glutGet(GLUT_SCREEN_HEIGHT)};
    #else
        size = (Vector) {glutGet(GLUT_SCREEN_HEIGHT), glutGet(GLUT_SCREEN_HEIGHT)};
    #endif
    glutInitWindowSize(size.x, size.y);
    glutInitWindowPosition(0, 0);
    glutCreateWindow("");

    glutDisplayFunc(&display);
    glutIdleFunc(&display);
    glutKeyboardFunc(&keyboard);
    glutPassiveMotionFunc(&mouse);
    glutMotionFunc(&mouse);
    glutMouseFunc(&mouse_event);
    glDisable(GL_LIGHTING);
    init_oct_points();
    glutFullScreen();

    last_time = __time_get_current_timestamp_internal();

    first_sys = NULL;

    glutMainLoop();
    return 0;
}