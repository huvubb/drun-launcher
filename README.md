# Drun Launcher

Windows 极速启动器：模糊搜索 + 即输即开，告别桌面图标。`drun <名称>` 秒开任意程序。

## 快速开始

### 1. 准备数据

创建 `exe-map.json`（名称 → 路径映射）：

```json
{
    "myapp": "D:\\Tools\\myapp.exe",
    "notepad": "D:\\Tools\\editor.exe",
    "示例工具": "D:\\tools\\示例工具.exe"
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

### 4. One-Click Install

Download `drun-setup.exe` from [Releases](https://github.com/huvubb/drun-launcher/releases), run it, choose language, done!

### 5. Usage

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
| `generate_data.ps1` | 从 JSON 生成 C++ 数据文件 |

## 项目结构

```
src/
  drun_main.cpp        # 主启动器
  drun_data.h          # 数据结构头文件
  drun_plus.cpp        # 管理工具（添加/移除/列出）
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

---

## ⚠️ 免责声明

本软件不提供任何形式的明示或暗示担保。
使用即视为您同意自行承担所有风险。
开发者对因使用本软件而导致的任何数据丢失、系统损坏或其他损失概不负责。
