# Drun Launcher

一键启动桌面任意 exe——260+ 程序，`drun <名称>` 秒开。

## 快速开始

### 1. 准备数据

创建 `exe-map.json`（名称 → 路径映射）：

```json
{
    "photoshop": "D:\\Adobe\\Photoshop.exe",
    "vscode": "D:\\Tools\\Code.exe",
    "搞机工具箱": "D:\\tools\\搞机工具箱.exe"
}
```

### 2. 生成数据文件

```powershell
.\scripts\generate_data.ps1 -JsonPath exe-map.json -OutputPath drun_data.cpp
```

### 3. 编译

```bash
g++ -o drun.exe src/drun_main.cpp drun_data.cpp -static -municode -mconsole -O2 -s
g++ -o drun-plus.exe src/drun_plus.cpp -static -municode -mconsole -O2 -s
```

### 4. 使用

```powershell
drun                    # 列出所有程序
drun photoshop          # 启动 Photoshop
drun 搞机               # 模糊搜索
drun-plus C:\app.exe    # 动态添加 exe（自动重编）
drun-plus --remove app  # 移除注册
```

## 功能

| 组件 | 说明 |
|------|------|
| `drun.exe` | 主启动器：精确匹配 + 模糊搜索 |
| `drun-plus.exe` | 管理工具：添加/移除/列出 exe，自动重编 drun |
| `pycompiler.exe` | Python 编译工具：强制编译 .py → .pyc，失败时自动复制重试 |
| `generate_data.ps1` | 从 JSON 生成 C++ 数据文件 |

## 项目结构

```
src/
  drun_main.cpp        # 主启动器
  drun_data.h          # 数据结构头文件
  drun_plus.cpp        # 管理工具（添加/移除/列出）
  pycompiler/          # Python 编译工具
    pycompiler.h
    pycompiler.cpp     # DLL 编译引擎
    pycompiler_main.cpp
scripts/
  drun.cmd             # CMD 包装器
  drun.ps1             # PowerShell 包装器
  generate_data.ps1    # JSON → C++ 数据生成
```

## 编译要求

- MinGW-w64 (g++)
- Windows SDK
- 静态链接，无需额外运行时

## License

MIT
