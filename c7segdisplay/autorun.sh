SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)

chmod +x $SCRIPT_DIR/run.sh

grep -qF "$SCRIPT_DIR/run.sh &" ~/.bash_profile    || echo "$SCRIPT_DIR/run.sh &" >> ~/.bash_profile

grep -qF "sudo modprobe g_ether &" ~/.bash_profile || echo "sudo modprobe g_ether &" >> ~/.bash_profile

echo "$USER ALL=(ALL) NOPASSWD: /sbin/modprobe g_ether" | sudo tee -a /etc/sudoers.d/g_ether