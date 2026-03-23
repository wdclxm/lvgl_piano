# Piano

这是一个基于 LVGL 的电子钢琴项目，支持两种运行方式：
- PC 模拟版：用于在 Linux/Ubuntu 桌面环境调试界面、交互和音频。
- `gec6818` 板端版：用于交叉编译后部署到开发板运行。

## 工程结构

- [main.c](/e:/Ubuntu16.04/share_with_win/C/piano/main.c)：主业务逻辑、UI、跟弹学习、录制、自动演奏、隐藏功能。
- [platform_pc.c](/e:/Ubuntu16.04/share_with_win/C/piano/platform_pc.c)：PC 平台显示、输入、音频适配。
- [platform_board.c](/e:/Ubuntu16.04/share_with_win/C/piano/platform_board.c)：开发板显示、输入、音频适配。
- [platform.h](/e:/Ubuntu16.04/share_with_win/C/piano/platform.h)：平台抽象接口。
- [Makefile](/e:/Ubuntu16.04/share_with_win/C/piano/Makefile)：PC/板端双目标编译入口。
- `music/`：跟弹学习曲目伴奏。
- `bgm/`：提示音与倒计时音频，例如 `begin.wav`。
- `aduio/`：钢琴键默认音色。
- `myaduio/`：隐藏功能中可替换到琴键上的自定义音色。
- `movie/`：启动动画资源。
- `config/`：运行时生成的配置文件，例如隐藏列表状态。
- `records/`：录制保存的曲目。

## 1. 编译 PC 端

PC 端默认生成可执行文件：

```bash
piano_app_sim
```

### 依赖

在 Ubuntu/Linux 下先安装：

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

如果 `pkg-config` 找不到 SDL2，可以使用 `sdl2-config` 手动传参：

```bash
make clean
make pc PC_SDL_CFLAGS="$(sdl2-config --cflags)" PC_SDL_LIBS="$(sdl2-config --libs)"
```

如果 SDL2 已安装，但 `pkg-config` 仍然找不到，可以先设置环境变量：

```bash
export PKG_CONFIG_PATH=/usr/lib/x86_64-linux-gnu/pkgconfig:$PKG_CONFIG_PATH
source ~/.bashrc
```

然后检查：

```bash
pkg-config --modversion sdl2
```

如果你不想依赖 `pkg-config` 或 `sdl2-config`，也可以直接手动传入 SDL2 头文件和库路径，例如：

```bash
make clean
make pc PC_SDL_CFLAGS="-I/usr/include/SDL2 -D_REENTRANT" PC_SDL_LIBS="-L/usr/lib/x86_64-linux-gnu -lSDL2"
```

如果 SDL2 安装在其他位置，把上面的 `-I` 和 `-L` 路径替换成你本机的实际路径即可。

### 运行

```bash
./piano_app_sim
```

说明：
- 鼠标左键模拟触摸。
- PC 端支持界面、按钮音符反馈、钢琴发声、跟弹伴奏、自动演奏。
- 如果系统中存在 `mplayer` 且 `movie/boluo.avi` 存在，启动时会先播放启动动画。

## 2. 编译 `gec6818` 板端

板端默认生成可执行文件：

```bash
piano_app
```

### 默认交叉编译器

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
- `aduio/*.wav`：钢琴键默认音色。
- `music/*.wav`：跟弹学习伴奏。
- `bgm/begin.wav`：跟弹学习倒计时提示音。
- `myaduio/*.wav`：隐藏功能可替换音色。
- `movie/boluo.avi`：启动动画。
- `/dev/fb0`：显示设备。
- `/dev/input/event0`：触摸输入。
- `/dev/dsp`：音频输出。

### 运行

把 `piano_app` 和资源目录拷贝到板端后，在程序目录执行：

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

## 4. 功能说明

- 默认 `make` 等价于 `make board`。
- 运行配置、隐藏列表和管理状态会保存到 `config/`。
- 录制内容会保存到 `records/`。
- 应用大厅支持右上角钢琴音量控制。
- 应用大厅空白区域支持 3 秒内连续点击 6 次进入隐藏功能；点击空白时也会触发钢琴音和飘出音符。
- 隐藏功能界面保留钢琴，但支持通过右上角三横线小圆框打开替换面板，自定义任意琴键音色。
- 隐藏功能中的目标琴键下拉框只显示真实存在的琴键，并映射回原始键位索引。
- 隐藏功能里按钮点击只会飘出随机颜色音符，不会额外触发随机钢琴音。
- 跟弹学习开始前会同步播放 `bgm/begin.wav`，配合 `3 / 2 / 1 / 开始` 倒计时动画。
- 跟弹学习支持分别调节钢琴与伴奏音量，其它页面仅调节钢琴音量。
- 跟弹学习和自动演奏结束后会延迟 500ms 再弹出结束弹窗。
- 跟弹学习和自动演奏结束弹窗都提供 `再来一次`、`选择其他曲目`、`返回大厅` 和右上角关闭按钮。
