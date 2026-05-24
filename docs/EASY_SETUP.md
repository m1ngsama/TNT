# TNT Quick Setup

This guide gets a TNT server running and explains the first user session.
For the full reference, see [README.md](../README.md), [tnt(1)](../tnt.1),
and [Deployment](DEPLOYMENT.md).

## Install

```sh
curl -sSL https://raw.githubusercontent.com/m1ngsama/TNT/main/install.sh | sh
```

Or build from source:

```sh
git clone https://github.com/m1ngsama/TNT.git
cd TNT
make
sudo make install
```

## Start A Server

```sh
tnt
```

By default TNT listens on port `2222`, stores `host_key` and `messages.log`
in the current directory, and allows anonymous SSH login.

Use explicit state and port settings for a long-running server:

```sh
tnt -p 2222 -d /var/lib/tnt
```

## Connect

```sh
ssh -p 2222 chat.example.com
```

Default access rules:

- Any SSH username is accepted.
- Empty or arbitrary passwords are accepted.
- SSH keys are not required.
- TNT asks for a display name after the SSH session starts.

Set `TNT_ACCESS_TOKEN` when you want a shared password:

```sh
TNT_ACCESS_TOKEN="change-this-password" tnt -p 2222 -d /var/lib/tnt
```

## First Session

TNT opens in INSERT mode. Type a message and press `Enter`.

Common keys:

```text
Esc         enter NORMAL mode
i           return to INSERT mode
:           enter COMMAND mode
?           open the full key reference
G or End    jump to latest messages
Ctrl+C      disconnect from NORMAL mode
```

Common commands:

```text
:help                 concise manual
:users                online users
:nick <name>          change nickname
:msg <user> <message> send private message
:inbox                show private messages
:last [N]             recent messages
:search <keyword>     search message history
:lang en|zh           switch UI language
:q                    disconnect
```

NORMAL mode opens at the latest messages and follows new messages until the
user scrolls up. Use `G` or `End` to return to the live tail.

## Configure

```sh
# Listening address and port
TNT_BIND_ADDR=0.0.0.0 PORT=2222 tnt

# State directory
TNT_STATE_DIR=/var/lib/tnt tnt

# Shared SSH password
TNT_ACCESS_TOKEN="change-this-password" tnt

# Default UI language; unset means locale detection, then English fallback
TNT_LANG=en tnt
TNT_LANG=zh tnt

# Connection limits
TNT_MAX_CONNECTIONS=200 tnt
TNT_MAX_CONN_PER_IP=30 tnt
TNT_MAX_CONN_RATE_PER_IP=60 tnt

# Idle timeout in seconds; 0 disables it
TNT_IDLE_TIMEOUT=3600 tnt
```

## Run Under systemd

```sh
sudo useradd -r -s /bin/false tnt
sudo mkdir -p /var/lib/tnt
sudo chown tnt:tnt /var/lib/tnt
sudo cp tnt.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now tnt
sudo systemctl status tnt
```

Put runtime overrides in `/etc/default/tnt`:

```sh
PORT=2222
TNT_BIND_ADDR=0.0.0.0
TNT_STATE_DIR=/var/lib/tnt
TNT_MAX_CONNECTIONS=200
TNT_MAX_CONN_PER_IP=30
TNT_MAX_CONN_RATE_PER_IP=60
TNT_RATE_LIMIT=1
TNT_SSH_LOG_LEVEL=1
TNT_PUBLIC_HOST=chat.example.com
```

Open the listening port in your firewall:

```sh
sudo ufw allow 2222/tcp
```

## Troubleshooting

### Port Already In Use

```sh
tnt -p 3333
```

### Cannot Connect

Check the server process:

```sh
systemctl status tnt
sudo journalctl -u tnt -n 50 --no-pager
```

Check the listening port:

```sh
ss -ltnp | grep 2222
```

Check the firewall:

```sh
sudo ufw status
```

### Connection Closes Immediately

The most common causes are per-IP connection limits, connection-rate limits,
an auth-failure ban, a full room, or a closed firewall port. The server logs
the rejection reason to stderr or the systemd journal.

## Chinese Quick Notes

### 安装

```sh
curl -sSL https://raw.githubusercontent.com/m1ngsama/TNT/main/install.sh | sh
```

### 启动

```sh
tnt
```

默认监听 `2222` 端口，并允许匿名 SSH 登录。

### 连接

```sh
ssh -p 2222 chat.example.com
```

默认情况下，任意 SSH 用户名和空密码都可以连接。进入后 TNT 会询问显示名称。

### 常用操作

```text
Enter        发送消息
Esc          进入 NORMAL 模式
i            回到 INSERT 模式
:            输入命令
?            查看完整按键参考
G 或 End      回到最新消息
:help        查看简明手册
:lang en|zh  切换界面语言
:q           断开连接
```

### 常用配置

```sh
TNT_ACCESS_TOKEN="change-this-password" tnt
TNT_STATE_DIR=/var/lib/tnt tnt
TNT_LANG=zh tnt
```
