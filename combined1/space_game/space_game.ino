#include <Wire.h>
#include <SPI.h>
#include "gfx_conf.h"
#include "spaceship_data.h"
#include "missile_sprites.h"
#include <LovyanGFX.hpp>
#include "input.h"  // Include input handling

//for wifi
#include <esp_now.h>

#define SPACESHIP_FRAME_COUNT 3
#define SPACESHIP_FRAME_WIDTH 32
#define SPACESHIP_FRAME_HEIGHT 32

#define MISSILE_FRAME_COUNT 1
#define MISSILE_FRAME_WIDTH 10
#define MISSILE_FRAME_HEIGHT 5

extern LGFX tft;
static LGFX_Sprite spaceship_sprite[SPACESHIP_FRAME_COUNT];
static LGFX_Sprite missile_sprite[MISSILE_FRAME_COUNT];
static LGFX_Sprite _sprites[2];

// variables for asteroids
struct ball_info_t {
  int32_t x;
  int32_t y;
  int32_t dx;
  int32_t dy;
  int32_t r;
  int32_t m;
  uint32_t color;
};

static constexpr std::uint32_t SHIFTSIZE = 2;
static constexpr std::uint32_t BALL_MAX = 15;

static ball_info_t _balls[2][BALL_MAX];
static std::uint32_t _ball_count = 0, _fps = 0;
static std::uint32_t ball_count = 0;
static std::uint32_t sec, psec;
static std::uint32_t fps = 0, frame_count = 0;

static std::uint32_t _width;
static std::uint32_t _height;

volatile bool _is_running;
volatile std::uint32_t _draw_count;
volatile std::uint32_t _loop_count;
unsigned long lastBallSpawnTime = 0;

// Spaceship positions and properties
struct Spaceship {
  int x;
  int y;
  int width;
  int height;
  int speed;
  int animationFrame;
  bool waiting;
  unsigned long waitStart;

  Spaceship(int startX, int startY) : x(startX), y(startY), width(SPACESHIP_FRAME_WIDTH), height(SPACESHIP_FRAME_HEIGHT), speed(5), animationFrame(0), waiting(false), waitStart(0) {}
};

Spaceship ship1(60, 240);
Spaceship ship2(740, 240);

// Missile properties
struct Missile {
  int x;
  int y;
  bool active;
  int animationFrame;

  Missile() : x(-1), y(-1), active(false), animationFrame(0) {}
};

Missile missile1;
Missile missile2;

void drawSpaceship(Spaceship &ship) {
  if(ship.x > 400){
    spaceship_sprite[ship.animationFrame].pushRotateZoom(&tft, ship.x, ship.y, 180, 1.5, 1.5);
  }else{
    spaceship_sprite[ship.animationFrame].pushRotateZoom(&tft, ship.x, ship.y, 0, 1.5, 1.5);
  }
}

void drawMissile(Missile &missile) {
  if (missile.active) {
    missile_sprite[missile.animationFrame].pushSprite(&tft, missile.x, missile.y);
  }else{
    missile_sprite[missile.animationFrame].deleteSprite();
  }
}


bool checkMissileBallCollision(Missile& missile, ball_info_t& ball) {
    if (!missile.active) return false;
    
    // Get missile center point
    int missileX = missile.x + (MISSILE_FRAME_WIDTH / 2);
    int missileY = missile.y + (MISSILE_FRAME_HEIGHT / 2);
    
    // Get ball position (converting from fixed point)
    int ballX = ball.x >> SHIFTSIZE;
    int ballY = ball.y >> SHIFTSIZE;
    int ballR = ball.r >> SHIFTSIZE;
    
    // Calculate distance between missile and ball centers
    int dx = missileX - ballX;
    int dy = missileY - ballY;
    int distance = sqrt(dx * dx + dy * dy);
    
    // Check if distance is less than ball radius plus missile size
    return distance < (ballR + MISSILE_FRAME_WIDTH/2);
}


static void diffDraw(LGFX_Sprite* sp0, LGFX_Sprite* sp1){
  union
  {
    std::uint32_t* s32;
    std::uint8_t* s;
  };
  union
  {
    std::uint32_t* p32;
    std::uint8_t* p;
  };
  s32 = (std::uint32_t*)sp0->getBuffer();
  p32 = (std::uint32_t*)sp1->getBuffer();

  auto width  = sp0->width();
  auto height = sp0->height();

  auto w32 = (width+3) >> 2;
  std::int32_t y = 0;
  do
  {
    std::int32_t x32 = 0;
    do
    {
      while (s32[x32] == p32[x32] && ++x32 < w32);
      if (x32 == w32) break;

      std::int32_t xs = x32 << 2;
      while (s[xs] == p[xs]) ++xs;

      while (++x32 < w32 && s32[x32] != p32[x32]);

      std::int32_t xe = (x32 << 2) - 1;
      if (xe >= width) xe = width - 1;
      while (s[xe] == p[xe]) --xe;

      tft.pushImage(xs, y, xe - xs + 1, 1, &s[xs]);
    } while (x32 < w32);
    s32 += w32;
    p32 += w32;
  } while (++y < height);
  tft.display();
}

static void drawfunc(void){
  ball_info_t *balls;
  ball_info_t *a;
  LGFX_Sprite *sprite;

  auto width  = _sprites[0].width();
  auto height = _sprites[0].height();

  std::size_t flip = _draw_count & 1;
  balls = &_balls[flip][0];

  sprite = &(_sprites[flip]);
  sprite->clear();

  //for (int32_t i = 8; i < width; i += 16) {
  //  sprite->drawFastVLine(i, 0, height, 0x1F);
  //}
  //for (int32_t i = 8; i < height; i += 16) {
  //  sprite->drawFastHLine(0, i, width, 0x1F);
  //}
  for (std::uint32_t i = 0; i < _ball_count; i++) {
    a = &balls[i];
    sprite->fillCircle( a->x >> SHIFTSIZE
                      , a->y >> SHIFTSIZE
                      , a->r >> SHIFTSIZE
                      , a->color);
  }

  sprite->setCursor(1,1);
  sprite->setTextColor(TFT_BLACK);
  sprite->printf("obj:%d fps:%d", _ball_count, _fps);
  sprite->setCursor(380, 0);
  sprite->setTextColor(TFT_WHITE);
  sprite->printf("obj:%d fps:%d", _ball_count, _fps);
  diffDraw(&_sprites[flip], &_sprites[!flip]);
  _draw_count++;
}

void spawnNewBall() {
  if (ball_count < BALL_MAX) {
    ball_info_t *newBall = &_balls[_loop_count & 1][ball_count];

    // Spawn from the center of the screen
    newBall->x = (_width / 2);
    newBall->y = (_height / 2);

    // Assign random velocities
    newBall->dx = ((rand() % 7) - 3) << SHIFTSIZE;
    newBall->dy = ((rand() % 7) - 3) << SHIFTSIZE;

    // Assign random color and size
    newBall->color = lgfx::color888(100 + (rand() % 155), 100 + (rand() % 155), 100 + (rand() % 155));
    newBall->r = (12 + (ball_count & 0x07)) << SHIFTSIZE;
    newBall->m = 12 + (ball_count & 0x07);

    ball_count++;
  }
}

static void mainfunc(void){
  static constexpr float e = 0.999; // Coefficient of friction

  sec = lgfx::millis() / 1000;
  if (psec != sec) {
    psec = sec;
    fps = frame_count;
    frame_count = 0;

    if (ball_count < BALL_MAX && (millis() - lastBallSpawnTime >= 3000)) {
      spawnNewBall();
      lastBallSpawnTime = millis();
    }

  #if defined (ESP32) || defined (CONFIG_IDF_TARGET_ESP32) || defined (ESP_PLATFORM)
      vTaskDelay(1);
  #endif
  }

  frame_count++;
  _loop_count++;

  ball_info_t *a, *b, *balls;
  int32_t rr, len, vx2vy2;
  float vx, vy, distance, t;

  size_t f = _loop_count & 1;
  balls = a = &_balls[f][0];
  b = &_balls[!f][0];
  memcpy(a, b, sizeof(ball_info_t) * ball_count);

  for (int i = 0; i != ball_count; i++) {
    a = &balls[i];
    
    // Apply velocity
    a->x += a->dx;
    a->y += a->dy;
    
    // Boundary collisions (keeping the improved boundary handling)
    if (a->x < a->r) {
        a->x = a->r;
        if (a->dx < 0) {
            a->dx = -a->dx * e;
            a->dy += (rand() % 3 - 1) << (SHIFTSIZE-2);
        }
    } else if (a->x >= _width - a->r) {
        a->x = _width - a->r - 1;
        if (a->dx > 0) {
            a->dx = -a->dx * e;
            a->dy += (rand() % 3 - 1) << (SHIFTSIZE-2);
        }
    }

    if (a->y < a->r) {
        a->y = a->r;
        if (a->dy < 0) {
            a->dy = -a->dy * e;
            a->dx += (rand() % 3 - 1) << (SHIFTSIZE-2);
        }
    } else if (a->y >= _height - a->r) {
        a->y = _height - a->r - 1;
        if (a->dy > 0) {
            a->dy = -a->dy * e;
            a->dx += (rand() % 3 - 1) << (SHIFTSIZE-2);
        }
    }

    // Ball-to-ball collisions
    for (int j = i + 1; j != ball_count; j++) {
        b = &balls[j];
        
        // Calculate distance between balls
        int32_t dx = a->x - b->x;
        int32_t dy = a->y - b->y;
        int32_t distanceSquared = (dx * dx + dy * dy) >> (SHIFTSIZE);;
        int32_t minDistance = (a->r + b->r) >> SHIFTSIZE;
        
        // Check for collision
        if (distanceSquared < minDistance * minDistance) {
            // Calculate collision normal
            float distance = sqrt((float)distanceSquared);
            if (distance == 0) distance = 1; // Prevent division by zero
            
            float nx = (float)dx / distance;
            float ny = (float)dy / distance;
            
            // Calculate relative velocity
            float dvx = (a->dx - b->dx) / (float)(1 << SHIFTSIZE);
            float dvy = (a->dy - b->dy) / (float)(1 << SHIFTSIZE);
            float velAlongNormal = dvx * nx + dvy * ny;
            
            // Don't resolve if objects are moving apart
            if (velAlongNormal > 0) continue;
            
            // Calculate restitution (bounciness)
            float restitution = 0.8f;
            
            // Calculate impulse scalar
            float j = -(1.0f + restitution) * velAlongNormal;
            j /= (1.0f / a->m + 1.0f / b->m);
            
            // Apply impulse
            float impulsex = j * nx;
            float impulsey = j * ny;
            
            // Convert back to fixed point and apply velocities
            a->dx += (int32_t)(impulsex / a->m); //* (1 << SHIFTSIZE);
            a->dy += (int32_t)(impulsey / a->m); //* (1 << SHIFTSIZE);
            b->dx -= (int32_t)(impulsex / b->m); //* (1 << SHIFTSIZE);
            b->dy -= (int32_t)(impulsey / b->m); //* (1 << SHIFTSIZE);
            
            // Separate the balls to prevent sticking
            float overlap = ((minDistance - distance) * (1 << SHIFTSIZE)) / 2.0f;
            a->x += (int32_t)(overlap * nx);
            a->y += (int32_t)(overlap * ny);
            b->x -= (int32_t)(overlap * nx);
            b->y -= (int32_t)(overlap * ny);
            
            // Add tiny random adjustment to prevent balls from getting stuck
            //if (abs(a->dx) < (1) && abs(a->dy) < (1)) {
            //    a->dx += (rand() % 3 - 1) << (SHIFTSIZE-2);
            //    a->dy += (rand() % 3 - 1) << (SHIFTSIZE-2);
            //}
            //if (abs(b->dx) < (1) && abs(b->dy) < (1)) {
            //    b->dx += (rand() % 3 - 1) << (SHIFTSIZE-2);
            //    b->dy += (rand() % 3 - 1) << (SHIFTSIZE-2);
            //}
        }
      }
  }
  _fps = fps;
  _ball_count = ball_count;
}


  #if defined (ESP32) || defined (CONFIG_IDF_TARGET_ESP32) || defined (ESP_PLATFORM)
  static void taskDraw(void*)
  {
    while ( _is_running )
    {
      while (_loop_count < _draw_count) { taskYIELD(); }
      drawfunc();
    }
    vTaskDelete(NULL);
  }
  #endif



void setup() {
  Serial.begin(115200);

  // Initialize ESP-NOW
  initESPNow();

  // Display Prepare
  tft.init();
  tft.begin(); // might be issue here
  tft.startWrite();
  tft.fillScreen(TFT_BLACK);
  tft.setTextSize(3);

  tft.setColorDepth(8);
  if (tft.width() < tft.height()) tft.setRotation(tft.getRotation() ^ 1);

  //auto tft_width = tft.width();
  //auto tft_height = tft.height();
  auto tft_width = tft.width();
  auto tft_height = tft.height();


  for (std::uint32_t i = 0; i < 2; ++i)
  {
    _sprites[i].setTextSize(2);
    _sprites[i].setColorDepth(8);
  }

  bool fail = false;
  for (std::uint32_t i = 0; !fail && i < 2; ++i)
  {
    fail = !_sprites[i].createSprite(tft_width, tft_height);
  }

  if (fail)
  {
    fail = false;
    for (std::uint32_t i = 0; !fail && i < 2; ++i)
    {
      _sprites[i].setPsram(true);
      fail = !_sprites[i].createSprite(tft_width, tft_height);
    }

    if (fail)
    {
      fail = false;
      if (tft_width > 800) tft_width = 800;
      if (tft_height > 480) tft_height = 480;

      for (std::uint32_t i = 0; !fail && i < 2; ++i)
      {
        _sprites[i].setPsram(true);
        fail = !_sprites[i].createSprite(tft_width, tft_height);
      }
      if (fail)
      {
        tft.print("createSprite fail...");
        lgfx::delay(3000);
      }
    }
  }

  _width = tft_width << SHIFTSIZE;
  _height = tft_height << SHIFTSIZE;
  if (ball_count < BALL_MAX) {
    for (std::uint32_t i = 0; i < ball_count; ++i) {
        auto a = &_balls[_loop_count & 1][i];
        a->color = lgfx::color888(100+(rand()%155), 100+(rand()%155), 100+(rand()%155));
        // Distribute balls across the screen instead of clustering them
        a->x = (rand() % (_width - (8 << SHIFTSIZE))) + (4 << SHIFTSIZE);
        a->y = (rand() % (_height - (8 << SHIFTSIZE))) + (4 << SHIFTSIZE);
        // Increase initial velocity range for more dynamic movement
        a->dx = ((rand() % 7) - 3) << SHIFTSIZE;
        a->dy = ((rand() % 7) - 3) << SHIFTSIZE;
        a->r = (4 + (i & 0x07)) << SHIFTSIZE;
        a->m = 4 + (i & 0x07);
    }
  }

  _is_running = true;
  _draw_count = 0;
  _loop_count = 0;

  #if defined (CONFIG_IDF_TARGET_ESP32)
    xTaskCreate(taskDraw, "taskDraw", 2048, NULL, 0, NULL);
  #endif

  // Load spaceship sprites from bitmap data in header file
  for (int i = 0; i < SPACESHIP_FRAME_COUNT; i++) {
    spaceship_sprite[i].createSprite(SPACESHIP_FRAME_WIDTH, SPACESHIP_FRAME_HEIGHT);
    spaceship_sprite[i].setSwapBytes(true);
    spaceship_sprite[i].pushImage(0, 0, SPACESHIP_FRAME_WIDTH, SPACESHIP_FRAME_HEIGHT, spaceship_data[i]);
  }

  // Load missile sprites from bitmap data in header file (assuming missile_data is similar to spaceship_data)
  for (int i = 0; i < MISSILE_FRAME_COUNT; i++) {
    missile_sprite[i].createSprite(MISSILE_FRAME_WIDTH, MISSILE_FRAME_HEIGHT);
    missile_sprite[i].setSwapBytes(true);
    missile_sprite[i].pushImage(0, 0, MISSILE_FRAME_WIDTH, MISSILE_FRAME_HEIGHT, missile_data[i]); // Placeholder for missile data
  }

  // Draw initial spaceships
  drawSpaceship(ship1);
  drawSpaceship(ship2);
}

void loop() {
  unsigned long currentMillis = millis();

  mainfunc();
  #if defined (CONFIG_IDF_TARGET_ESP32)
    while (_loop_count < _draw_count) { taskYIELD(); }
  #else
    drawfunc();
  #endif

  // Simulate ship1 movement
  if (ship1.waiting) {
    if (currentMillis - ship1.waitStart >= 1000) {
      ship1.waiting = false;
      ship1.animationFrame = (ship1.speed > 0) ? 0 : 2; // Set sprite frame for movement
    }
  } else {
    tft.fillRect(ship1.x, ship1.y, ship1.width, ship1.height, TFT_BLACK);
    ship1.y += ship1.speed;
    if (ship1.y <= 0 || ship1.y >= tft.height() - ship1.height) {
      ship1.speed *= -1;
      ship1.waiting = true;
      ship1.waitStart = currentMillis;
      ship1.animationFrame = 1; // Set sprite frame for waiting
    }
    drawSpaceship(ship1);
  }

  // Simulate ship2 movement
  if (ship2.waiting) {
    if (currentMillis - ship2.waitStart >= 1000) {
      ship2.waiting = false;
      ship2.animationFrame = (ship2.speed > 0) ? 2 : 0; // Set sprite frame for movement
    }
  } else {
    tft.fillRect(ship2.x, ship2.y, ship2.width, ship2.height, TFT_BLACK);
    ship2.y += ship2.speed;
    if (ship2.y <= 0 || ship2.y >= tft.height() - ship2.height) {
      ship2.speed *= -1;
      ship2.waiting = true;
      ship2.waitStart = currentMillis;
      ship2.animationFrame = 1; // Set sprite frame for waiting
    }
    drawSpaceship(ship2);
  }

  // Simulate ship1 firing
  if (!missile1.active && random(0, 100) < 10) {  // Random chance to fire
    missile1.x = ship1.x + ship1.width;
    missile1.y = ship1.y + (ship1.height / 2);
    missile1.active = true;
  }

  // Simulate ship2 firing
  if (!missile2.active && random(0, 100) < 10) {  // Random chance to fire
    missile2.x = ship2.x - MISSILE_FRAME_WIDTH;
    missile2.y = ship2.y + (ship2.height / 2);
    missile2.active = true;
  }

  if (missile1.active) {
    // Check collision with all balls
    bool collision = false;
    for (uint32_t i = 0; i < ball_count && !collision; i++) {
        ball_info_t* ball = &_balls[_loop_count & 1][i];
        if (checkMissileBallCollision(missile1, *ball)) {
            // Handle collision
            missile1.active = false;
            
            // Remove the ball by shifting all balls after it one position back
            for (uint32_t j = i; j < ball_count - 1; j++) {
                _balls[_loop_count & 1][j] = _balls[_loop_count & 1][j + 1];
                _balls[!(_loop_count & 1)][j] = _balls[!(_loop_count & 1)][j + 1];
            }
            ball_count--;
            collision = true;
            
            // Clear the missile from screen
            tft.fillRect(missile1.x, missile1.y, MISSILE_FRAME_WIDTH, MISSILE_FRAME_HEIGHT, TFT_BLACK);
        }
    }

    // Update missiles
    if (missile1.active) {
      tft.fillRect(missile1.x, missile1.y, MISSILE_FRAME_WIDTH, MISSILE_FRAME_HEIGHT, TFT_BLACK);
      missile1.x += 10;
      if (missile1.x > tft.width()) {
        missile1.active = false;
      } else {
        drawMissile(missile1);
      }
    }
  }



  if (missile2.active) {
    // Check collision with all balls
    bool collision = false;
    for (uint32_t i = 0; i < ball_count && !collision; i++) {
        ball_info_t* ball = &_balls[_loop_count & 1][i];
        if (checkMissileBallCollision(missile2, *ball)) {
            // Handle collision
            missile2.active = false;
            
            // Remove the ball by shifting all balls after it one position back
            for (uint32_t j = i; j < ball_count - 1; j++) {
                _balls[_loop_count & 1][j] = _balls[_loop_count & 1][j + 1];
                _balls[!(_loop_count & 1)][j] = _balls[!(_loop_count & 1)][j + 1];
            }
            ball_count--;
            collision = true;
            
            // Clear the missile from screen
            tft.fillRect(missile2.x, missile2.y, MISSILE_FRAME_WIDTH, MISSILE_FRAME_HEIGHT, TFT_BLACK);
        }
    }
    
    if (missile2.active) {
        tft.fillRect(missile2.x, missile2.y, MISSILE_FRAME_WIDTH, MISSILE_FRAME_HEIGHT, TFT_BLACK);
        missile2.x -= 10;
        if (missile2.x < 0) {
            missile2.active = false;
        } else {
            drawMissile(missile2);
        }
    }
  }



  delay(50);
}
