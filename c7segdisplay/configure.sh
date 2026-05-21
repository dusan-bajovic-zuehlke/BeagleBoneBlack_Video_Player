sudo apt update
sudo apt install gcc -y
sudo apt install pkg-config -y
sudo apt install libavformat-dev -y
sudo apt install libswscale-dev -y
sudo apt install libavcodec-dev -y
sudo apt install libavutil-dev -y


sudo systemctl disable keyboard-setup
sudo systemctl disable systemd-resolved
sudo systemctl set-default multi-user.target


sudo mkdir -p /etc/systemd/system/getty@tty1.service.d/

sudo tee /etc/systemd/system/getty@tty1.service.d/override.conf << EOF 
[Service]
ExecStart=
ExecStart=-/sbin/agetty --autologin $USER --noclear %I \$TERM
EOF

sudo systemctl daemon-reload
