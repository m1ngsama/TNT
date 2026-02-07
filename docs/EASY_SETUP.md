# TNT 匿名聊天室 - 快速部署指南 / TNT Anonymous Chat - Quick Setup Guide

[中文](#中文) | [English](#english)

---

## 中文

### 一键安装

```bash
curl -sSL https://raw.githubusercontent.com/m1ngsama/TNT/main/install.sh | sh
```

### 启动服务器

```bash
tnt              # 监听 2222 端口
```

就这么简单！服务器已经运行了。

### 用户如何连接

用户只需要一个SSH客户端即可，无需任何配置：

```bash
ssh -p 2222 your.server.ip
```

**重要提示**：
- ✅ 用户可以使用**任意用户名**连接
- ✅ 用户可以输入**任意密码**（甚至直接按回车跳过）
- ✅ **不需要SSH密钥**
- ✅ 不需要提前注册账号
- ✅ 完全匿名，零门槛

连接后，系统会提示输入显示名称（也可以留空使用默认名称）。

### 生产环境部署

使用 systemd 让服务器开机自启：

```bash
# 1. 创建专用用户
sudo useradd -r -s /bin/false tnt
sudo mkdir -p /var/lib/tnt
sudo chown tnt:tnt /var/lib/tnt

# 2. 安装服务
sudo cp tnt.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable tnt
sudo systemctl start tnt

# 3. 检查状态
sudo systemctl status tnt
```

### 防火墙设置

记得开放2222端口：

```bash
# Ubuntu/Debian
sudo ufw allow 2222/tcp

# CentOS/RHEL
sudo firewall-cmd --permanent --add-port=2222/tcp
sudo firewall-cmd --reload
```

### 可选配置

通过环境变量进行高级配置：

```bash
# 修改端口
PORT=3333 tnt

# 限制最大连接数
TNT_MAX_CONNECTIONS=100 tnt

# 限制每个IP的最大连接数
TNT_MAX_CONN_PER_IP=10 tnt

# 只允许本地访问
TNT_BIND_ADDR=127.0.0.1 tnt

# 添加访问密码（所有用户共用一个密码）
TNT_ACCESS_TOKEN="your_secret_password" tnt
```

**注意**：设置 `TNT_ACCESS_TOKEN` 后，所有用户必须使用该密码才能连接，这会提高安全性但也会增加使用门槛。

### 特性

- 🚀 **零配置** - 开箱即用
- 🔓 **完全匿名** - 无需注册，无需密钥
- 🎨 **Vim风格界面** - 支持 INSERT/NORMAL/COMMAND 三种模式
- 📜 **消息历史** - 自动保存聊天记录
- 🌐 **UTF-8支持** - 完美支持中英文及其他语言
- 🔒 **可选安全特性** - 支持限流、访问控制等

---

## English

### One-Line Installation

```bash
curl -sSL https://raw.githubusercontent.com/m1ngsama/TNT/main/install.sh | sh
```

### Start Server

```bash
tnt              # Listen on port 2222
```

That's it! Your server is now running.

### How Users Connect

Users only need an SSH client, no configuration required:

```bash
ssh -p 2222 your.server.ip
```

**Important**:
- ✅ Users can use **ANY username**
- ✅ Users can enter **ANY password** (or just press Enter to skip)
- ✅ **No SSH keys required**
- ✅ No registration needed
- ✅ Completely anonymous, zero barrier

After connecting, the system will prompt for a display name (can be left empty for default name).

### Production Deployment

Use systemd for auto-start on boot:

```bash
# 1. Create dedicated user
sudo useradd -r -s /bin/false tnt
sudo mkdir -p /var/lib/tnt
sudo chown tnt:tnt /var/lib/tnt

# 2. Install service
sudo cp tnt.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable tnt
sudo systemctl start tnt

# 3. Check status
sudo systemctl status tnt
```

### Firewall Configuration

Remember to open port 2222:

```bash
# Ubuntu/Debian
sudo ufw allow 2222/tcp

# CentOS/RHEL
sudo firewall-cmd --permanent --add-port=2222/tcp
sudo firewall-cmd --reload
```

### Optional Configuration

Advanced configuration via environment variables:

```bash
# Change port
PORT=3333 tnt

# Limit max connections
TNT_MAX_CONNECTIONS=100 tnt

# Limit connections per IP
TNT_MAX_CONN_PER_IP=10 tnt

# Bind to localhost only
TNT_BIND_ADDR=127.0.0.1 tnt

# Add password protection (shared password for all users)
TNT_ACCESS_TOKEN="your_secret_password" tnt
```

**Note**: Setting `TNT_ACCESS_TOKEN` requires all users to use that password to connect. This increases security but also raises the barrier to entry.

### Features

- 🚀 **Zero Configuration** - Works out of the box
- 🔓 **Fully Anonymous** - No registration, no keys
- 🎨 **Vim-Style Interface** - Supports INSERT/NORMAL/COMMAND modes
- 📜 **Message History** - Automatic chat log persistence
- 🌐 **UTF-8 Support** - Perfect for all languages
- 🔒 **Optional Security** - Rate limiting, access control, etc.

---

## 使用示例 / Usage Examples

### 基本使用 / Basic Usage

```bash
# 启动服务器
tnt

# 用户连接（从任何机器）
ssh -p 2222 chat.example.com
# 输入任意密码或直接回车
# 输入显示名称或留空
# 开始聊天！
```

### Vim风格操作 / Vim-Style Operations

连接后：

- **INSERT 模式**（默认）：直接输入消息，按 Enter 发送
- **NORMAL 模式**：按 `ESC` 进入，使用 `j/k` 滚动历史，`g/G` 跳转顶部/底部
- **COMMAND 模式**：按 `:` 进入，输入 `:list` 查看在线用户，`:help` 查看帮助

### 故障排除 / Troubleshooting

#### 问题：端口已被占用

```bash
# 更换端口
tnt -p 3333
```

#### 问题：防火墙阻止连接

```bash
# 检查防火墙状态
sudo ufw status
sudo firewall-cmd --list-ports

# 确保已开放端口
sudo ufw allow 2222/tcp
```

#### 问题：连接超时

```bash
# 检查服务器是否运行
ps aux | grep tnt

# 检查端口监听
sudo lsof -i:2222
```

---

## 技术细节 / Technical Details

- **语言**: C
- **依赖**: libssh
- **并发**: 多线程，支持数百个同时连接
- **安全**: 可选限流、访问控制、密码保护
- **存储**: 简单的文本日志（messages.log）
- **配置**: 环境变量，无配置文件

---

## 许可证 / License

MIT License - 自由使用、修改、分发
