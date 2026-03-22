# Piano

这是一个基于 LVGL 的电子钢琴项目，支持两种运行方式：

- PC 模拟版：用于在 Linux/Ubuntu 桌面环境调试界面、交互、音频
- `gec6818` 板端版：用于交叉编译后部署到开发板运行

## 工程结构

- [main.c](/e:/Ubuntu16.04/share_with_win/C/piano/main.c)：主业务逻辑、UI、跟弹、录制、自动演奏
- [platform_pc.c](/e:/Ubuntu16.04/share_with_win/C/piano/platform_pc.c)：PC 平台显示/输入/音频适配
- [platform_board.c](/e:/Ubuntu16.04/share_with_win/C/piano/platform_board.c)：板端显示/输入/音频适配
- [Makefile](/e:/Ubuntu16.04/share_with_win/C/piano/Makefile)：双目标编译入口
- `music/`：跟弹学习伴奏
- `aduio/`：钢琴键音效
- `movie/`：启动动画
- `config/`：程序运行时生成的配置文件
- `records/`：录制保存的曲目

## 1. 编译 PC 端

PC 端默认生成可执行文件：

```bash
piano_app_sim
```

### 依赖

在 Ubuntu/Linux 下需要先安装：

```bash
sudo apt update
sudo apt install build-essential libsdl2-dev pkg-config
```

如果需要播放启动动画，还可以安装：

```bash
sudo apt install mplayer
```

### 编译命令

如果 `pkg-config` 可以正常找到 SDL2：

```bash
make clean
make pc
```

如果 `pkg-config` 找不到 SDL2，可以手动传参数：

```bash
make clean
make pc PC_SDL_CFLAGS="$(sdl2-config --cflags)" PC_SDL_LIBS="$(sdl2-config --libs)"
```

如果你的系统已经装了 SDL2，但 `pkg-config` 还是找不到，可以先设置环境变量：

```bash
export PKG_CONFIG_PATH=/usr/lib/x86_64-linux-gnu/pkgconfig:$PKG_CONFIG_PATH
source ~/.bashrc
```

然后重新检查：

```bash
pkg-config --modversion sdl2
```

如果你不想依赖 `pkg-config` 或 `sdl2-config`，也可以直接手动传入 SDL2 的头文件和库路径，例如：

```bash
make clean
make pc PC_SDL_CFLAGS="-I/usr/include/SDL2 -D_REENTRANT" PC_SDL_LIBS="-L/usr/lib/x86_64-linux-gnu -lSDL2"
```

如果你的 SDL2 安装在其他位置，把上面的 `-I` 和 `-L` 路径替换成你自己的实际路径即可。

### 运行

```bash
./piano_app_sim
```

说明：

- 鼠标左键模拟触摸
- PC 端支持界面、按钮音效、钢琴发声、跟弹伴奏、自动演奏
- 如果系统中存在 `mplayer` 且 `movie/boluo.avi` 存在，启动时会先播放启动动画

## 2. 编译 `gec6818` 板端

板端默认生成可执行文件：

```bash
piano_app
```

### 默认编译器

[Makefile](/e:/Ubuntu16.04/share_with_win/C/piano/Makefile) 默认使用：

```bash
arm-linux-gcc
```

### 编译命令

直接编译：

```bash
make clean
make board
```

如果你的交叉编译器名字不同，可以显式指定：

```bash
make clean
make board BOARD_CC=arm-linux-gcc
```

例如如果你的工具链是 `arm-linux-gnueabihf-gcc`：

```bash
make clean
make board BOARD_CC=arm-linux-gnueabihf-gcc
```

### 运行前准备

运行板端程序前，请确保下列资源和设备存在：

- `aduio/*.wav`：钢琴键音频
- `music/*.wav`：跟弹学习伴奏
- `movie/boluo.avi`：启动动画
- `/dev/fb0`：显示设备
- `/dev/input/event0`：触摸输入
- `/dev/dsp`：音频输出

### 运行

将 `piano_app` 和资源目录拷到板端后，在程序目录执行：

```bash
./piano_app
```

## 3. 常用命令

编译 PC 端：

```bash
make pc
```

编译板端：

```bash
make board
```

清理：

```bash
make clean
```

## 4. 说明

- 默认目标是板端版本，所以直接执行 `make` 等价于 `make board`
- 当前项目的运行配置、隐藏曲目列表等信息会保存到 `config/`
- 录制内容会保存到 `records/`
