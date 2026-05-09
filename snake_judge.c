#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/resource.h>
#include <sys/select.h>
#include <errno.h>

#define MAP_SIZE 20
#define MAX_SNAKE_LENGTH (MAP_SIZE * MAP_SIZE)

#define EMPTY '.'
#define WALL '#'
#define HEAD 'H'
#define BODY 'B'
#define FOOD 'F'
#define OBSTACLE 'O'

typedef struct {
    int x, y;
} Position;

typedef struct {
    char map[MAP_SIZE][MAP_SIZE + 1];
    Position snake[MAX_SNAKE_LENGTH];
    int snakeLength;
    int direction;
    int score;
    int moveCount;
    int N;
    int gameOver;
    int deathReason;
} GameState;

typedef struct {
    Position *positions;
    int count;
    int current;
} FoodSequence;

static FoodSequence foodSeq = {NULL, 0, 0};

// 从文件描述符读取一个字符，带有超时限制 (毫秒)
int readCharWithTimeout(int fd, char *c, int timeoutMs) {
    struct timeval tv;
    fd_set fds;
    
    // 计算截止时间
    struct timespec start_ts, current_ts;
    clock_gettime(CLOCK_MONOTONIC, &start_ts);
    long long timeoutNs = (long long)timeoutMs * 1000000LL;
    
    while (1) {
        FD_ZERO(&fds);
        FD_SET(fd, &fds);
        
        clock_gettime(CLOCK_MONOTONIC, &current_ts);
        long long elapsedNs = (current_ts.tv_sec - start_ts.tv_sec) * 1000000000LL + (current_ts.tv_nsec - start_ts.tv_nsec);
        long long remainingNs = timeoutNs - elapsedNs;
        
        if (remainingNs <= 0) return 0; // Timeout
        
        tv.tv_sec = remainingNs / 1000000000LL;
        tv.tv_usec = (remainingNs % 1000000000LL) / 1000;
        
        int ret = select(fd + 1, &fds, NULL, NULL, &tv);
        if (ret > 0) {
            int n = read(fd, c, 1);
            return n == 1 ? 1 : 0; // If n==0 (EOF), return 0
        } else if (ret == 0) {
            return 0; // Timeout
        } else {
            if (errno == EINTR) continue;
            return 0; // Error
        }
    }
}

// 带有超时的格式化读取：跳过空白符，读取一个字符和一个整数
int readMoveAndScoreWithTimeout(int fd, char *move, int *score, int timeoutMs) {
    char c;
    // 1. 跳过空白符，读取 move
    while (1) {
        if (!readCharWithTimeout(fd, &c, timeoutMs)) return 0;
        if (c != ' ' && c != '\n' && c != '\r' && c != '\t') {
            *move = c;
            break;
        }
    }
    // 2. 跳过空白符，读取 score 的第一个字符
    char scoreBuf[32];
    int pos = 0;
    while (1) {
        if (!readCharWithTimeout(fd, &c, timeoutMs)) return 0;
        if (c != ' ' && c != '\n' && c != '\r' && c != '\t') {
            scoreBuf[pos++] = c;
            break;
        }
    }
    // 3. 继续读取 score 直到遇到空白符或 EOF
    while (pos < 31) {
        if (!readCharWithTimeout(fd, &c, timeoutMs)) break; // 如果超时，可能是整数已经输出完毕，且没有后续空白符（虽然不规范，但也算读完）
        if (c == ' ' || c == '\n' || c == '\r' || c == '\t') break;
        scoreBuf[pos++] = c;
    }
    scoreBuf[pos] = '\0';
    *score = atoi(scoreBuf);
    return 1;
}

// 从文件描述符读取一个字符串 (遇到空白符停止)，带有超时限制
int readStringWithTimeout(int fd, char *buf, int maxLen, int timeoutMs) {
    char c;
    int pos = 0;
    // 1. 跳过空白符
    while (1) {
        if (!readCharWithTimeout(fd, &c, timeoutMs)) return 0;
        if (c != ' ' && c != '\n' && c != '\r' && c != '\t') {
            buf[pos++] = c;
            break;
        }
    }
    // 2. 读取字符串
    while (pos < maxLen - 1) {
        if (!readCharWithTimeout(fd, &c, timeoutMs)) {
            // 如果遇到EOF但已经读了内容，应该返回1
            break;
        }
        if (c == ' ' || c == '\n' || c == '\r' || c == '\t') break;
        buf[pos++] = c;
    }
    buf[pos] = '\0';
    return pos > 0 ? 1 : 0;
}

// 读取整数
int readIntWithTimeout(int fd, int *val, int timeoutMs) {
    char buf[32];
    if (!readStringWithTimeout(fd, buf, sizeof(buf), timeoutMs)) return 0;
    *val = atoi(buf);
    return 1;
}

void initGameState(GameState *state) {
    memset(state, 0, sizeof(GameState));
    state->snakeLength = 0;
    state->score = 0;
    state->moveCount = 0;
    state->gameOver = 0;
    state->deathReason = 0;
    state->direction = 0;
}

void printMap(const GameState *state) {
    for (int i = 0; i < MAP_SIZE; i++) {
        printf("%s\n", state->map[i]);
    }
}

void generateEmptyMap(GameState *state) {
    for (int i = 0; i < MAP_SIZE; i++) {
        for (int j = 0; j < MAP_SIZE; j++) {
            if (i == 0 || i == MAP_SIZE - 1 || j == 0 || j == MAP_SIZE - 1) {
                state->map[i][j] = WALL;
            } else {
                state->map[i][j] = EMPTY;
            }
        }
        state->map[i][MAP_SIZE] = '\0';
    }
}

void generateRandomObstacles(GameState *state, int count) {
    int placed = 0;
    while (placed < count) {
        int x = 1 + rand() % (MAP_SIZE - 2);
        int y = 1 + rand() % (MAP_SIZE - 2);
        if (state->map[y][x] == EMPTY) {
            state->map[y][x] = OBSTACLE;
            placed++;
        }
    }
}

void placeSnake(GameState *state, int startX, int startY, int length, int dir) {
    state->snake[0].x = startX;
    state->snake[0].y = startY;
    state->map[startY][startX] = HEAD;
    state->snakeLength = 1;
    state->direction = dir;
    
    int dx[4] = {0, -1, 0, 1};
    int dy[4] = {-1, 0, 1, 0};
    
    for (int i = 1; i < length; i++) {
        int nx = state->snake[i-1].x - dx[dir];
        int ny = state->snake[i-1].y - dy[dir];
        if (nx > 0 && nx < MAP_SIZE - 1 && ny > 0 && ny < MAP_SIZE - 1) {
            state->snake[i].x = nx;
            state->snake[i].y = ny;
            state->map[ny][nx] = BODY;
            state->snakeLength++;
        } else {
            break;
        }
    }
}

void generateContinuousObstacleSegment(GameState *state, int length) {
    int placed = 0;
    int maxTries = 100;
    while (placed == 0 && maxTries-- > 0) {
        int x = 1 + rand() % (MAP_SIZE - 2);
        int y = 1 + rand() % (MAP_SIZE - 2);
        int dir = rand() % 4;
        int dx[] = {0, 1, 0, -1};
        int dy[] = {-1, 0, 1, 0};
        
        int canFit = 1;
        for (int i = 0; i < length; i++) {
            int nx = x + i * dx[dir];
            int ny = y + i * dy[dir];
            if (nx <= 0 || nx >= MAP_SIZE - 1 || ny <= 0 || ny >= MAP_SIZE - 1 || state->map[ny][nx] != EMPTY) {
                canFit = 0;
                break;
            }
        }
        
        if (canFit) {
            for (int i = 0; i < length; i++) {
                int nx = x + i * dx[dir];
                int ny = y + i * dy[dir];
                state->map[ny][nx] = OBSTACLE;
                placed++;
            }
        }
    }
}

// --- Flood Fill 连通性检查 ---
int isMapConnected(GameState *state) {
    int visited[MAP_SIZE][MAP_SIZE] = {0};
    int startX = -1, startY = -1;
    int emptyCount = 0;

    // 寻找一个空地作为起点，并统计所有的空地数量
    for (int y = 1; y < MAP_SIZE - 1; y++) {
        for (int x = 1; x < MAP_SIZE - 1; x++) {
            if (state->map[y][x] == EMPTY) {
                emptyCount++;
                if (startX == -1) {
                    startX = x;
                    startY = y;
                }
            }
        }
    }

    if (startX == -1) return 1; // 没有空地，虽然不合理但认为"连通"

    // BFS 或 DFS
    int queueX[MAP_SIZE * MAP_SIZE];
    int queueY[MAP_SIZE * MAP_SIZE];
    int head = 0, tail = 0;

    queueX[tail] = startX;
    queueY[tail] = startY;
    tail++;
    visited[startY][startX] = 1;
    int connectedCount = 1;

    int dx[] = {0, 1, 0, -1};
    int dy[] = {-1, 0, 1, 0};

    while (head < tail) {
        int cx = queueX[head];
        int cy = queueY[head++];

        for (int i = 0; i < 4; i++) {
            int nx = cx + dx[i];
            int ny = cy + dy[i];

            if (nx > 0 && nx < MAP_SIZE - 1 && ny > 0 && ny < MAP_SIZE - 1) {
                if (!visited[ny][nx] && state->map[ny][nx] == EMPTY) {
                    visited[ny][nx] = 1;
                    queueX[tail] = nx;
                    queueY[tail] = ny;
                    tail++;
                    connectedCount++;
                }
            }
        }
    }

    return connectedCount == emptyCount;
}

void generateMapEasy(GameState *state) {
    generateEmptyMap(state);
    int startX = 5 + rand() % 10;
    int startY = 5 + rand() % 10;
    placeSnake(state, startX, startY, 3, 0);
    state->N = 5;
}

void generateMapMediumScatter(GameState *state) {
    generateEmptyMap(state);
    generateRandomObstacles(state, 5 + rand() % 6);
    int startX = 5 + rand() % 10;
    int startY = 5 + rand() % 10;
    int maxTries = 1000;
    while (maxTries-- > 0) {
        startX = 5 + rand() % 10;
        startY = 5 + rand() % 10;
        
        if (state->map[startY][startX] == EMPTY &&
            startY + 1 < MAP_SIZE - 1 && state->map[startY + 1][startX] == EMPTY &&
            startY + 2 < MAP_SIZE - 1 && state->map[startY + 2][startX] == EMPTY) {
            break;
        }
    }
    placeSnake(state, startX, startY, 3, 0);
    state->N = 5;
}

void generateMapMediumWall(GameState *state) {
    generateEmptyMap(state);
    generateContinuousObstacleSegment(state, 5 + rand() % 6);
    int startX = 5 + rand() % 10;
    int startY = 5 + rand() % 10;
    int maxTries = 1000;
    while (maxTries-- > 0) {
        startX = 5 + rand() % 10;
        startY = 5 + rand() % 10;
        
        if (state->map[startY][startX] == EMPTY &&
            startY + 1 < MAP_SIZE - 1 && state->map[startY + 1][startX] == EMPTY &&
            startY + 2 < MAP_SIZE - 1 && state->map[startY + 2][startX] == EMPTY) {
            break;
        }
    }
    placeSnake(state, startX, startY, 3, 0);
    state->N = 5;
}

void generateMapHardScatter(GameState *state) {
    generateEmptyMap(state);
    generateRandomObstacles(state, 10 + rand() % 11);
    int startX = 3 + rand() % 14;
    int startY = 3 + rand() % 14;
    int maxTries = 1000;
    while (maxTries-- > 0) {
        startX = 3 + rand() % 14;
        startY = 3 + rand() % 14;
        
        // Check if there is enough space for the snake body (length 3, direction 0 which means facing UP)
        // dir = 0 (UP), dx = 0, dy = -1. The body will be placed below the head (ny = head.y - (-1) = head.y + 1)
        if (state->map[startY][startX] == EMPTY &&
            startY + 1 < MAP_SIZE - 1 && state->map[startY + 1][startX] == EMPTY &&
            startY + 2 < MAP_SIZE - 1 && state->map[startY + 2][startX] == EMPTY) {
            break;
        }
    }
    placeSnake(state, startX, startY, 3, 0);
    state->N = 4;
}

void generateMapHardWall(GameState *state) {
    generateEmptyMap(state);
    generateContinuousObstacleSegment(state, 5 + rand() % 6);
    generateContinuousObstacleSegment(state, 5 + rand() % 6);
    int startX = 3 + rand() % 14;
    int startY = 3 + rand() % 14;
    int maxTries = 1000;
    while (maxTries-- > 0) {
        startX = 3 + rand() % 14;
        startY = 3 + rand() % 14;
        
        if (state->map[startY][startX] == EMPTY &&
            startY + 1 < MAP_SIZE - 1 && state->map[startY + 1][startX] == EMPTY &&
            startY + 2 < MAP_SIZE - 1 && state->map[startY + 2][startX] == EMPTY) {
            break;
        }
    }
    placeSnake(state, startX, startY, 3, 0);
    state->N = 4;
}

void generateIrregularWall(GameState *state, int blocks) {
    int maxTries = 100;
    while (maxTries-- > 0) {
        int x = 1 + rand() % (MAP_SIZE - 2);
        int y = 1 + rand() % (MAP_SIZE - 2);
        if (state->map[y][x] != EMPTY) continue;
        
        state->map[y][x] = OBSTACLE;
        int placed = 1;
        
        while (placed < blocks) {
            int dir = rand() % 4;
            int dx[] = {0, 1, 0, -1};
            int dy[] = {-1, 0, 1, 0};
            int nx = x + dx[dir];
            int ny = y + dy[dir];
            
            // Allow checking adjacent blocks to cluster them, like tetris
            if (nx > 0 && nx < MAP_SIZE - 1 && ny > 0 && ny < MAP_SIZE - 1 && state->map[ny][nx] == EMPTY) {
                state->map[ny][nx] = OBSTACLE;
                // DO NOT always update x and y, to make it a cluster instead of a long line
                if (rand() % 2 == 0) {
                    x = nx;
                    y = ny;
                }
                placed++;
            } else {
                int found = 0;
                for (int i = 0; i < 4; i++) {
                    nx = x + dx[i];
                    ny = y + dy[i];
                    if (nx > 0 && nx < MAP_SIZE - 1 && ny > 0 && ny < MAP_SIZE - 1 && state->map[ny][nx] == EMPTY) {
                        state->map[ny][nx] = OBSTACLE;
                        x = nx;
                        y = ny;
                        placed++;
                        found = 1;
                        break;
                    }
                }
                if (!found) break;
            }
        }
        if (placed > 1) break;
    }
}

void generateMapMediumMixed(GameState *state) {
    while (1) {
        generateEmptyMap(state);
        // 3个零散点
        generateRandomObstacles(state, 3);
        // 一个5格墙
        generateContinuousObstacleSegment(state, 5);
        // 3个不规则障碍物
        for (int i = 0; i < 3; i++) {
            generateIrregularWall(state, 4); // 像俄罗斯方块通常4格
        }
        
        if (isMapConnected(state)) break;
    }
    
    int startX = 5 + rand() % 10;
    int startY = 5 + rand() % 10;
    int maxTries = 1000;
    while (maxTries-- > 0) {
        startX = 5 + rand() % 10;
        startY = 5 + rand() % 10;
        
        if (state->map[startY][startX] == EMPTY &&
            startY + 1 < MAP_SIZE - 1 && state->map[startY + 1][startX] == EMPTY &&
            startY + 2 < MAP_SIZE - 1 && state->map[startY + 2][startX] == EMPTY) {
            break;
        }
    }
    placeSnake(state, startX, startY, 3, 0);
    state->N = 5;
}

void generateMapHardMixed(GameState *state) {
    while (1) {
        generateEmptyMap(state);
        // 5个零散点
        generateRandomObstacles(state, 5);
        // 一个7格墙
        generateContinuousObstacleSegment(state, 7);
        // 5个不规则障碍物
        for (int i = 0; i < 5; i++) {
            generateIrregularWall(state, 4);
        }
        
        if (isMapConnected(state)) break;
    }
    
    int startX = 3 + rand() % 14;
    int startY = 3 + rand() % 14;
    int maxTries = 1000;
    while (maxTries-- > 0) {
        startX = 3 + rand() % 14;
        startY = 3 + rand() % 14;
        
        if (state->map[startY][startX] == EMPTY &&
            startY + 1 < MAP_SIZE - 1 && state->map[startY + 1][startX] == EMPTY &&
            startY + 2 < MAP_SIZE - 1 && state->map[startY + 2][startX] == EMPTY) {
            break;
        }
    }
    placeSnake(state, startX, startY, 3, 0);
    state->N = 4;
}

void generateMapUltimate(GameState *state) {
    generateEmptyMap(state);
    generateRandomObstacles(state, 10 + rand() % 11);
    
    // Add 4 clusters of irregular walls (tetris-like)
    for (int i = 0; i < 4; i++) {
        generateIrregularWall(state, 6 + rand() % 5);
    }
    
    int startX = 3 + rand() % 14;
    int startY = 3 + rand() % 14;
    int maxTries = 1000;
    while (maxTries-- > 0) {
        startX = 3 + rand() % 14;
        startY = 3 + rand() % 14;
        
        if (state->map[startY][startX] == EMPTY &&
            startY + 1 < MAP_SIZE - 1 && state->map[startY + 1][startX] == EMPTY &&
            startY + 2 < MAP_SIZE - 1 && state->map[startY + 2][startX] == EMPTY) {
            break;
        }
    }
    placeSnake(state, startX, startY, 3, 0);
    state->N = 3;
}

int loadMapFromFile(GameState *state, const char *filename) {
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        fprintf(stderr, "错误: 无法打开地图文件 %s\n", filename);
        return 0;
    }
    
    for (int i = 0; i < MAP_SIZE; i++) {
        if (fscanf(fp, "%s", state->map[i]) != 1) {
            fprintf(stderr, "错误: 地图文件格式不正确\n");
            fclose(fp);
            return 0;
        }
    }
    
    if (fscanf(fp, "%d", &state->N) != 1) {
        fprintf(stderr, "错误: 无法读取N值\n");
        fclose(fp);
        return 0;
    }
    
    fclose(fp);
    
    state->snakeLength = 0;
    for (int i = 0; i < MAP_SIZE; i++) {
        for (int j = 0; j < MAP_SIZE; j++) {
            if (state->map[i][j] == HEAD) {
                state->snake[0].x = j;
                state->snake[0].y = i;
                state->snakeLength = 1;
            }
        }
    }
    
    if (state->snakeLength == 0) {
        fprintf(stderr, "错误: 地图中没有找到蛇头(H)\n");
        return 0;
    }
    
    int found = 1;
    while (found && state->snakeLength < MAX_SNAKE_LENGTH) {
        found = 0;
        int lastX = state->snake[state->snakeLength - 1].x;
        int lastY = state->snake[state->snakeLength - 1].y;
        int dx[] = {0, 0, -1, 1};
        int dy[] = {-1, 1, 0, 0};
        
        for (int i = 0; i < 4; i++) {
            int nx = lastX + dx[i];
            int ny = lastY + dy[i];
            if (nx >= 0 && nx < MAP_SIZE && ny >= 0 && ny < MAP_SIZE) {
                if (state->map[ny][nx] == BODY) {
                    int alreadyIn = 0;
                    for (int k = 0; k < state->snakeLength; k++) {
                        if (state->snake[k].x == nx && state->snake[k].y == ny) {
                            alreadyIn = 1;
                            break;
                        }
                    }
                    if (!alreadyIn) {
                        state->snake[state->snakeLength].x = nx;
                        state->snake[state->snakeLength].y = ny;
                        state->snakeLength++;
                        found = 1;
                        break;
                    }
                }
            }
        }
    }
    
    return 1;
}

// BFS function to check if food is reachable from snake head
int isReachable(GameState *state, int startX, int startY, int targetX, int targetY) {
    if (startX == targetX && startY == targetY) return 1;
    
    int visited[MAP_SIZE][MAP_SIZE] = {0};
    
    Position queue[MAP_SIZE * MAP_SIZE];
    int head = 0, tail = 0;
    
    queue[tail].x = startX;
    queue[tail].y = startY;
    tail++;
    visited[startY][startX] = 1;
    
    int dx[] = {0, 0, -1, 1};
    int dy[] = {-1, 1, 0, 0};
    
    while (head < tail) {
        Position curr = queue[head++];
        
        if (curr.x == targetX && curr.y == targetY) {
            return 1;
        }
        
        for (int i = 0; i < 4; i++) {
            int nx = curr.x + dx[i];
            int ny = curr.y + dy[i];
            
            if (nx >= 0 && nx < MAP_SIZE && ny >= 0 && ny < MAP_SIZE) {
                // Ignore snake BODY, because snake will move. We only care if the space is permanently blocked by walls or obstacles
                if (!visited[ny][nx] && state->map[ny][nx] != WALL && state->map[ny][nx] != OBSTACLE) {
                    visited[ny][nx] = 1;
                    queue[tail].x = nx;
                    queue[tail].y = ny;
                    tail++;
                }
            }
        }
    }
    
    return 0;
}

void generateFoodRandom(GameState *state, int *foodX, int *foodY, const char *difficulty) {
    int headX = state->snake[0].x;
    int headY = state->snake[0].y;
    
    // In ultimate mode, only spawn food in regions reachable by snake head (ignoring snake body, only considering walls/obstacles)
    int isUltimate = (difficulty && strcmp(difficulty, "ultimate") == 0);
    
    Position reachableEmpty[MAP_SIZE * MAP_SIZE];
    int emptyCount = 0;
    
    for (int i = 0; i < MAP_SIZE; i++) {
        for (int j = 0; j < MAP_SIZE; j++) {
            if (state->map[i][j] == EMPTY) {
                if (!isUltimate || isReachable(state, headX, headY, j, i)) {
                    reachableEmpty[emptyCount].x = j;
                    reachableEmpty[emptyCount].y = i;
                    emptyCount++;
                }
            }
        }
    }
    
    if (emptyCount == 0) {
        // If no reachable empty spot, fallback to any empty spot (might be dead end, but no choice)
        int fallbackCount = 0;
        Position fallbackEmpty[MAP_SIZE * MAP_SIZE];
        for (int i = 0; i < MAP_SIZE; i++) {
            for (int j = 0; j < MAP_SIZE; j++) {
                if (state->map[i][j] == EMPTY) {
                    fallbackEmpty[fallbackCount].x = j;
                    fallbackEmpty[fallbackCount].y = i;
                    fallbackCount++;
                }
            }
        }
        if (fallbackCount == 0) {
            *foodX = -1;
            *foodY = -1;
            return;
        }
        int target = rand() % fallbackCount;
        *foodX = fallbackEmpty[target].x;
        *foodY = fallbackEmpty[target].y;
        return;
    }
    
    int target = rand() % emptyCount;
    *foodX = reachableEmpty[target].x;
    *foodY = reachableEmpty[target].y;
}

void generateFoodFarFromHead(GameState *state, int *foodX, int *foodY) {
    int headX = state->snake[0].x;
    int headY = state->snake[0].y;
    int bestX = -1, bestY = -1;
    int maxDist = -1;
    
    for (int i = 1; i < MAP_SIZE - 1; i++) {
        for (int j = 1; j < MAP_SIZE - 1; j++) {
            if (state->map[i][j] == EMPTY) {
                int dist = abs(j - headX) + abs(i - headY);
                if (dist > maxDist) {
                    maxDist = dist;
                    bestX = j;
                    bestY = i;
                }
            }
        }
    }
    
    *foodX = bestX;
    *foodY = bestY;
}

int getNextFood(GameState *state, int *foodX, int *foodY) {
    if (foodSeq.current < foodSeq.count) {
        *foodX = foodSeq.positions[foodSeq.current].x;
        *foodY = foodSeq.positions[foodSeq.current].y;
        foodSeq.current++;
        return 1;
    }
    return 0;
}

int isValidMove(GameState *state, char move) {
    int dx[4] = {0, -1, 0, 1};
    int dy[4] = {-1, 0, 1, 0};
    char moves[4] = {'W', 'A', 'S', 'D'};
    
    int moveDir = -1;
    for (int i = 0; i < 4; i++) {
        if (moves[i] == move) {
            moveDir = i;
            break;
        }
    }
    
    if (moveDir == -1) return 0;
    
    if ((state->direction == 0 && moveDir == 2) ||
        (state->direction == 2 && moveDir == 0) ||
        (state->direction == 1 && moveDir == 3) ||
        (state->direction == 3 && moveDir == 1)) {
        return 0;
    }
    
    int newX = state->snake[0].x + dx[moveDir];
    int newY = state->snake[0].y + dy[moveDir];
    
    if (newX < 0 || newX >= MAP_SIZE || newY < 0 || newY >= MAP_SIZE) {
        return 0;
    }
    
    if (state->map[newY][newX] == WALL || state->map[newY][newX] == OBSTACLE) {
        return 0;
    }
    
    // 检查蛇身碰撞，但排除蛇尾（因为蛇移动后尾巴会离开）
    for (int i = 0; i < state->snakeLength - 1; i++) {
        if (state->snake[i].x == newX && state->snake[i].y == newY) {
            return 0;
        }
    }
    
    return 1;
}

void moveSnake(GameState *state, char move) {
    int dx[4] = {0, -1, 0, 1};
    int dy[4] = {-1, 0, 1, 0};
    char moves[4] = {'W', 'A', 'S', 'D'};
    
    int moveDir = -1;
    for (int i = 0; i < 4; i++) {
        if (moves[i] == move) {
            moveDir = i;
            break;
        }
    }
    
    if (moveDir == -1) {
        state->gameOver = 1;
        state->deathReason = 4;
        return;
    }
    
    Position tail = state->snake[state->snakeLength - 1];
    state->direction = moveDir;
    
    for (int i = state->snakeLength - 1; i > 0; i--) {
        state->snake[i] = state->snake[i - 1];
    }
    
    state->snake[0].x += dx[moveDir];
    state->snake[0].y += dy[moveDir];
    
    int newX = state->snake[0].x;
    int newY = state->snake[0].y;
    
    if (newX < 0 || newX >= MAP_SIZE || newY < 0 || newY >= MAP_SIZE) {
        state->gameOver = 1;
        state->deathReason = 1;
        return;
    }
    
    if (state->map[newY][newX] == WALL || state->map[newY][newX] == OBSTACLE) {
        state->gameOver = 1;
        state->deathReason = 2;
        return;
    }
    
    for (int i = 1; i < state->snakeLength; i++) {
        if (state->snake[i].x == newX && state->snake[i].y == newY) {
            state->gameOver = 1;
            state->deathReason = 3;
            return;
        }
    }
    
    int ateFood = 0;
    if (state->map[newY][newX] == FOOD) {
        ateFood = 1;
        state->score += 10;
    }
    
    state->moveCount++;
    int shouldGrow = ateFood || (state->moveCount % state->N == 0);
    
    if (shouldGrow) {
        state->snake[state->snakeLength] = tail;
        state->snakeLength++;
    }
    
    for (int i = 0; i < MAP_SIZE; i++) {
        for (int j = 0; j < MAP_SIZE; j++) {
            if (state->map[i][j] == HEAD || state->map[i][j] == BODY) {
                state->map[i][j] = EMPTY;
            }
        }
    }
    
    for (int i = 0; i < state->snakeLength; i++) {
        if (i == 0) {
            state->map[state->snake[i].y][state->snake[i].x] = HEAD;
        } else {
            state->map[state->snake[i].y][state->snake[i].x] = BODY;
        }
    }
}

void parseFoodSequence(const char *seq) {
    char *copy = strdup(seq);
    char *token = strtok(copy, ";");
    int count = 0;
    
    while (token) {
        count++;
        token = strtok(NULL, ";");
    }
    
    free(copy);
    copy = strdup(seq);
    
    foodSeq.positions = (Position *)malloc(count * sizeof(Position));
    foodSeq.count = count;
    foodSeq.current = 0;
    
    token = strtok(copy, ";");
    int idx = 0;
    while (token && idx < count) {
        int x, y;
        if (sscanf(token, "%d,%d", &y, &x) == 2) {
            foodSeq.positions[idx].x = x;
            foodSeq.positions[idx].y = y;
            idx++;
        }
        token = strtok(NULL, ";");
    }
    
    free(copy);
}

void printUsage(const char *prog) {
    printf("用法: %s <AI程序路径> [选项]\n", prog);
    printf("\n选项:\n");
    printf("  -m <mode>     地图模式: random(随机), fixed(固定), file(文件)\n");
    printf("  -f <file>     地图文件路径 (mode=file时使用)\n");
    printf("  -d <diff>     难度: easy(简单), medium-scatter(中等零散), medium-wall(中等连续), hard-scatter(困难零散), hard-wall(困难连续)\n");
    printf("  -s <seed>     随机种子 (用于可重复测试)\n");
    printf("  -F <mode>     食物模式: random(随机), far(远离蛇头), seq(固定序列)\n");
    printf("  --food-seq    食物位置序列 (如: \"2,3;5,6;8,9\")\n");
    printf("  -v            显示每一步的地图\n");
    printf("  -p            仅输出初始地图并退出 (不进行游戏)\n");
    printf("  -h            显示帮助\n");
    printf("\n注意: 游戏会一直运行直到蛇死亡或程序异常退出\n");
    printf("\n示例:\n");
    printf("  %s ./snake_ai -m random -d medium\n", prog);
    printf("  %s ./snake_ai -m file -f test_map.txt\n", prog);
    printf("  %s ./snake_ai -m random -s 12345 -v\n", prog);
}

int main(int argc, char *argv[]) {
    // 先检查是否有帮助选项
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            printUsage(argv[0]);
            return 0;
        }
    }
    
    if (argc < 2) {
        printUsage(argv[0]);
        return 1;
    }
    
    char *aiPath = argv[1];
    char *mapMode = "random";
    char *mapFile = NULL;
    char *difficulty = "medium";
    char *foodMode = "random";
    char *foodSeqStr = NULL;
    int customN = -1;
    int seed = time(NULL);
    int seed_provided = 0; // 标记是否手动提供了种子
    int verbose = 0;
    int printOnly = 0;
    
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-m") == 0 && i + 1 < argc) {
            mapMode = argv[++i];
        } else if (strcmp(argv[i], "-f") == 0 && i + 1 < argc) {
            mapFile = argv[++i];
        } else if (strcmp(argv[i], "-d") == 0 && i + 1 < argc) {
            difficulty = argv[++i];
        } else if (strcmp(argv[i], "-n") == 0 && i + 1 < argc) {
            customN = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
            seed = atoi(argv[++i]);
            seed_provided = 1;
        } else if (strcmp(argv[i], "-F") == 0 && i + 1 < argc) {
            foodMode = argv[++i];
        } else if (strcmp(argv[i], "--food-seq") == 0 && i + 1 < argc) {
            foodSeqStr = argv[++i];
        } else if (strcmp(argv[i], "-v") == 0) {
            verbose = 1;
        } else if (strcmp(argv[i], "-p") == 0) {
            printOnly = 1;
        }
    }
    
    srand(seed);
    
    if (strcmp(foodMode, "seq") == 0 && foodSeqStr) {
        parseFoodSequence(foodSeqStr);
    }
    
    GameState state;
    initGameState(&state);
    
    if (strcmp(mapMode, "file") == 0) {
        if (!mapFile) {
            fprintf(stderr, "错误: 文件模式需要指定地图文件 (-f)\n");
            return 1;
        }
        if (!loadMapFromFile(&state, mapFile)) {
            return 1;
        }
    } else if (strcmp(mapMode, "fixed") == 0) {
        if (mapFile) {
            if (!loadMapFromFile(&state, mapFile)) {
                return 1;
            }
        } else {
            generateMapMediumScatter(&state);
        }
    } else {
        if (strcmp(difficulty, "easy") == 0) {
            generateMapEasy(&state);
        } else if (strcmp(difficulty, "medium-scatter") == 0) {
            generateMapMediumScatter(&state);
        } else if (strcmp(difficulty, "medium-wall") == 0) {
            generateMapMediumWall(&state);
        } else if (strcmp(difficulty, "medium-mixed") == 0) {
            generateMapMediumMixed(&state);
        } else if (strcmp(difficulty, "hard-scatter") == 0) {
            generateMapHardScatter(&state);
        } else if (strcmp(difficulty, "hard-wall") == 0) {
            generateMapHardWall(&state);
        } else if (strcmp(difficulty, "hard-mixed") == 0) {
            generateMapHardMixed(&state);
        } else if (strcmp(difficulty, "ultimate") == 0) {
            generateMapUltimate(&state);
        } else {
            generateMapMediumScatter(&state);
        }
    }
    
    if (customN > 0) {
        state.N = customN;
    }
    
    if (printOnly) {
        printf("Seed: %d\n", seed);
        printMap(&state);
        return 0;
    }
    
    int pipeToAI[2], pipeFromAI[2];
    if (pipe(pipeToAI) == -1 || pipe(pipeFromAI) == -1) {
        perror("pipe");
        return 1;
    }
    
    pid_t pid = fork();
    if (pid == -1) {
        perror("fork");
        return 1;
    }
    
    if (pid == 0) {
        close(pipeToAI[1]);
        close(pipeFromAI[0]);
        dup2(pipeToAI[0], STDIN_FILENO);
        dup2(pipeFromAI[1], STDOUT_FILENO);
        close(pipeToAI[0]);
        close(pipeFromAI[1]);
        
        // 设置严格的资源限制 (OJ 标准)
        struct rlimit rl;
        // 内存限制: 64MB
        rl.rlim_cur = 64 * 1024 * 1024;
        rl.rlim_max = 64 * 1024 * 1024;
        setrlimit(RLIMIT_AS, &rl);
        
        // 栈限制: 8192KB
        rl.rlim_cur = 8192 * 1024;
        rl.rlim_max = 8192 * 1024;
        setrlimit(RLIMIT_STACK, &rl);
        
        execl(aiPath, aiPath, NULL);
        perror("execl");
        exit(1);
    }
    
    close(pipeToAI[0]);
    close(pipeFromAI[1]);
    
    FILE *aiIn = fdopen(pipeToAI[1], "w");
    
    if (!aiIn) {
        fprintf(stderr, "错误: 无法创建文件流\n");
        return 1;
    }
    
    // 禁用缓冲，确保实时通信
    setbuf(aiIn, NULL);
    
    for (int i = 0; i < MAP_SIZE; i++) {
        fprintf(aiIn, "%s\n", state.map[i]);
    }
    fprintf(aiIn, "%d\n", state.N);
    fflush(aiIn);
    
    printf("===== 评测开始 =====\n");
    printf("AI程序: %s\n", aiPath);
    printf("地图模式: %s\n", mapMode);
    printf("难度: %s\n", difficulty);
    printf("食物模式: %s\n", foodMode);
    printf("\n初始地图:\n");
    printMap(&state);
    printf("\n");
    
    int foodX = -1, foodY = -1;
    int hasFood = 0;
    
    while (!state.gameOver) {
        if (!hasFood) {
            if (strcmp(foodMode, "seq") == 0 && foodSeq.count > 0) {
                if (getNextFood(&state, &foodX, &foodY)) {
                    hasFood = 1;
                }
            } else if (strcmp(foodMode, "far") == 0) {
                generateFoodFarFromHead(&state, &foodX, &foodY);
                hasFood = (foodX != -1);
            } else {
                generateFoodRandom(&state, &foodX, &foodY, difficulty);
                hasFood = (foodX != -1);
            }
        }
        
        if (hasFood) {
            state.map[foodY][foodX] = FOOD;
            fprintf(aiIn, "%d %d\n", foodY, foodX);
        } else {
            fprintf(aiIn, "100 100\n");
        }
        fflush(aiIn);
        
        char move;
        int reportedScore;
        
        if (!readMoveAndScoreWithTimeout(pipeFromAI[0], &move, &reportedScore, 400)) {
            printf("AI单步超时(>400ms)、异常退出或格式错误，游戏结束\n");
            state.gameOver = 1;
            state.deathReason = 7; // 7 = TLE
            break;
        }
        
        if (reportedScore != state.score) {
            printf("警告: AI报告的得分(%d)与实际得分(%d)不符\n", reportedScore, state.score);
        }
        
        int validResult = isValidMove(&state, move);
        if (!validResult) {
            int dx[4] = {0, -1, 0, 1};
            int dy[4] = {-1, 0, 1, 0};
            char moves[4] = {'W', 'A', 'S', 'D'};
            int moveDir = -1;
            for (int i = 0; i < 4; i++) {
                if (moves[i] == move) {
                    moveDir = i;
                    break;
                }
            }
            
            printf("第%d步: 非法移动 '%c'，", state.moveCount + 1, move);
            
            // 检查是否是反向移动
            if (moveDir != -1 && 
                ((state.direction == 0 && moveDir == 2) ||
                 (state.direction == 2 && moveDir == 0) ||
                 (state.direction == 1 && moveDir == 3) ||
                 (state.direction == 3 && moveDir == 1))) {
                char *dirNames[4] = {"上", "左", "下", "右"};
                printf("原因: 反向移动 (当前方向%s, 试图向%s)\n", dirNames[state.direction], dirNames[moveDir]);
            } else if (moveDir != -1) {
                int newX = state.snake[0].x + dx[moveDir];
                int newY = state.snake[0].y + dy[moveDir];
                if (newX < 0 || newX >= MAP_SIZE || newY < 0 || newY >= MAP_SIZE) {
                    printf("原因: 移出边界\n");
                } else if (state.map[newY][newX] == WALL || state.map[newY][newX] == OBSTACLE) {
                    printf("原因: 撞到墙或障碍物\n");
                } else {
                    int hitSelf = 0;
                    for (int i = 0; i < state.snakeLength; i++) {
                        if (state.snake[i].x == newX && state.snake[i].y == newY) {
                            hitSelf = 1;
                            break;
                        }
                    }
                    if (hitSelf) {
                        printf("原因: 撞到自己身体\n");
                    } else {
                        printf("原因: 未知\n");
                    }
                }
            } else {
                printf("原因: 无效的方向字符\n");
            }
            
            state.gameOver = 1;
            state.deathReason = 4;
            break;
        }
        
        // 在移动前检查蛇头位置是否有食物
        int headX = state.snake[0].x;
        int headY = state.snake[0].y;
        
        // 根据移动方向计算新位置
        int dx[4] = {0, -1, 0, 1};
        int dy[4] = {-1, 0, 1, 0};
        char moves[4] = {'W', 'A', 'S', 'D'};
        int moveDir = -1;
        for (int i = 0; i < 4; i++) {
            if (moves[i] == move) {
                moveDir = i;
                break;
            }
        }
        
        if (moveDir != -1) {
            int newX = headX + dx[moveDir];
            int newY = headY + dy[moveDir];
            if (state.map[newY][newX] == FOOD) {
                hasFood = 0;
            }
        }
        
        moveSnake(&state, move);
        
        if (verbose) {
            printf("\n===== 第%d步移动后 =====\n", state.moveCount);
            printMap(&state);
            printf("移动: %c | 得分: %d | 蛇长: %d | 位置: (%d,%d)\n",
                   move, state.score, state.snakeLength,
                   state.snake[0].x, state.snake[0].y);
        } else {
            printf("第%3d步: 移动 %c | 得分: %3d | 蛇长: %2d | 位置: (%2d,%2d)\n",
                   state.moveCount, move, state.score, state.snakeLength,
                   state.snake[0].x, state.snake[0].y);
        }
    }
    
    // 发送游戏结束信号
    fprintf(aiIn, "100 100\n");
    fflush(aiIn);
    
    // 读取 AI 返回的最终地图和得分 (官方评测机要求)
    char finalMapLine[MAP_SIZE + 2];
    char aiFinalMap[MAP_SIZE][MAP_SIZE + 2];
    int finalReportedScore = -1;
    int mapMismatch = 0;
    int protocolError = 0;
    int eof_reached = 0;
    
    // 在最后阶段使用 fscanf 阻塞读取 (按照要求回滚为最初的逻辑)
    FILE *aiOut = fdopen(dup(pipeFromAI[0]), "r");
    if (!aiOut) {
        printf("错误: 无法创建读取流\n");
        protocolError = 1;
    } else {
        for (int i = 0; i < MAP_SIZE; i++) {
            if (fscanf(aiOut, "%s", finalMapLine) != 1) {
                eof_reached = 1;
                break;
            }
            strcpy(aiFinalMap[i], finalMapLine);
            if (strlen(finalMapLine) != MAP_SIZE) mapMismatch = 1;
            if (strcmp(finalMapLine, state.map[i]) != 0) mapMismatch = 1;
        }
        
        if (eof_reached) {
            printf("错误: 未能读取AI返回的最终地图 (AI可能在收到 100 100 信号后未输出内容便退出了)\n");
            protocolError = 1;
        } else {
            if (fscanf(aiOut, "%d", &finalReportedScore) == 1) {
                if (finalReportedScore != state.score) {
                    printf("错误: AI在结束时返回的最终得分(%d)与实际得分(%d)不符!\n", finalReportedScore, state.score);
                    protocolError = 1;
                }
            } else {
                printf("错误: 未能读取到AI返回的最终得分!\n");
                protocolError = 1;
            }
            
            if (!protocolError && mapMismatch) {
                printf("错误: AI返回的最终地图与评测机记录的地图不一致!\n");
                printf("评测机记录的最终地图:\n");
                printMap(&state);
                printf("AI返回的最终地图:\n");
                for (int i = 0; i < MAP_SIZE; i++) {
                    printf("%s\n", aiFinalMap[i]);
                }
                protocolError = 1;
            }
        }
        fclose(aiOut);
    }
    
    fclose(aiIn);
    close(pipeFromAI[0]);
    
    int status;
    waitpid(pid, &status, 0);
    
    if (protocolError) {
        state.score = 0; // 协议错误直接计0分
        state.gameOver = 1;
        state.deathReason = 6;
    }
    
    printf("\n===== 评测结果 =====\n");
    printf("N值: %d\n", state.N);
    if (seed_provided) {
        printf("随机种子: %d\n", seed);
    } else {
        printf("随机种子: %d (随机生成)\n", seed);
    }
    printf("最终得分: %d\n", state.score);
    printf("存活步数: %d\n", state.moveCount);
    printf("最终蛇长: %d\n", state.snakeLength);
    
    if (state.gameOver) {
        printf("死亡原因: ");
        switch (state.deathReason) {
            case 1: printf("撞墙(出界)\n"); break;
            case 2: printf("撞到障碍物\n"); break;
            case 3: printf("撞到自己身体\n"); break;
            case 4: printf("非法移动\n"); break;
            case 5: printf("AI程序异常\n"); break;
            case 6: printf("结束协议违规(计0分)\n"); break;
            default: printf("未知原因\n"); break;
        }
    } else {
        printf("结束原因: 正常结束\n");
    }
    
    if (protocolError && mapMismatch) {
        printf("\n评测机客观最终地图:\n");
        printMap(&state);
        
        printf("\nAI返回的错误最终地图:\n");
        for (int i = 0; i < MAP_SIZE; i++) {
            printf("%s\n", aiFinalMap[i]);
        }
    } else {
        printf("\n最终地图:\n");
        printMap(&state);
    }
    
    if (foodSeq.positions) {
        free(foodSeq.positions);
    }
    
    return 0;
}
