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

typedef struct Particle {
    VectorFloat position;
    VectorFloat linear_speed;
    float direction;
    float speed;
    char active;
} Particle;

typedef struct Trail {
    unsigned long since;
    VectorFloat position;
    float angle;
    struct Trail* next;
} Trail;

typedef struct ParticleSystem {
    Particle* current;
    unsigned long active;
    unsigned long count;
    struct ParticleSystem* next;
} ParticleSystem;

long long last_time;
VectorFloat mouse_pos = {0, 0};
float mouse_rot = 0;
ParticleSystem* first;

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

char update_particle(ParticleSystem* system, Particle* current, Vector dimensions, float deltatime) {
    current->position.x += sinf(current->direction) * current->speed * deltatime;
    current->position.y += cosf(current->direction) * current->speed * deltatime;
    current->linear_speed.y -= 200.0f * deltatime;
    current->position.x += current->linear_speed.x * deltatime;
    current->position.y += current->linear_speed.y * deltatime;
    if (current->position.x <= -dimensions.x || current->position.x >= dimensions.x) return 1;
    if (current->position.y <= -dimensions.y || current->position.y >= dimensions.y) return 1;
    draw_oct(current->position, 5, dimensions, (RGB) {255, 255, 255});
    return 0;
}

void update_system(ParticleSystem* system, Vector dimensions, float deltatime) {
    for (unsigned long i = 0; i < system->count; i++) {
        if (system->current[i].active) {
            if (update_particle(system, system->current + i, dimensions, deltatime)) {
                system->current[i].active = false;
                system->active--;
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
    glVertex2f(c1.x, c1.y);
    glVertex2f(c2.x, c2.y);
    glVertex2f(c2.x, c2.y);
    glVertex2f(c1.x, c1.y);
    if (trail->next) __render_trail(trail->next, width, existence, now, dimension);
}

void render_trail(Trail** data, unsigned long width, unsigned long existence, unsigned long now, 
            VectorFloat alignment, Vector dimension, RGB color) {
    Trail* new_trail = malloc(sizeof(Trail));
    new_trail->next = *data;
    new_trail->since = now;
    new_trail->position = alignment;
    new_trail->angle = mouse_rot;
    *data = new_trail;
    
    Trail* trail = new_trail;
    while(trail) {
        Trail* next = trail->next;
        Trail** next_data = &trail->next;
        float factor = (float) existence - (float) (now - trail->since);
        if (factor < 0) {
            if (data) {
                *data = NULL;
                data = NULL;
            }
            free(trail);
        }
        else {
            data = next_data;
        }
        trail = next;
    }
    if (new_trail) {
        glColor3ub(color.r, color.g, color.b);
        glBegin(GL_QUADS);
        VectorFloat c = point((VectorFloat) {
            mouse_pos.x,
            mouse_pos.y
        }, dimension);
        glVertex2f(c.x, c.y);
        glVertex2f(c.x, c.y);
        __render_trail(new_trail, width, existence, now, dimension);
        glEnd();
    }
}

Trail* trail_item = NULL;

void display() {
    long long now = __time_get_current_timestamp_internal();
    float deltatime = (now - last_time) / 10e5f;
    last_time = now;

    Vector dimensions;
    dimensions.x = glutGet(GLUT_WINDOW_WIDTH);
    dimensions.y = glutGet(GLUT_WINDOW_HEIGHT);
    glClearColor(0, 0, 0, 0.0);
    glClear(GL_COLOR_BUFFER_BIT);

    ParticleSystem* cursor = first;
    while (cursor) {
        //update_system(cursor, dimensions, deltatime);
        cursor = cursor->next;
    }
    //draw_oct(mouse_pos, 10, dimensions, (RGB) {255, 255, 255});
    render_trail(&trail_item, 10, 1000000, now, mouse_pos, dimensions, (RGB) {255, 255, 255});

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

ParticleSystem particle_system;

int main(int argc, char** argv) {
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
    glutInitWindowPosition(100, 100);
    glutCreateWindow("");

    glutDisplayFunc(&display);
    glutIdleFunc(&display);
    glutKeyboardFunc(&keyboard);
    glutPassiveMotionFunc(&mouse);
    glutMotionFunc(&mouse);
    glDisable(GL_LIGHTING);
    init_oct_points();
    glutFullScreen();

    last_time = __time_get_current_timestamp_internal();

    particle_system = (ParticleSystem) {
        malloc(sizeof(Particle) * 100),
        100,
        100,
        NULL
    };
    for (unsigned long i = 0; i < particle_system.count; i++) {
        particle_system.current[i].position.x = 0;
        particle_system.current[i].position.y = 0;
        particle_system.current[i].linear_speed.x = 0;
        particle_system.current[i].linear_speed.y = 0;
        particle_system.current[i].direction = ((float) random() / (float) RAND_MAX) * 2 * M_PI;
        particle_system.current[i].speed = ((float) random() / (float) RAND_MAX) * 100.0f + 200.0f;
        particle_system.current[i].active = true;
    }
    first = &particle_system;

    glutMainLoop();
    return 0;
}