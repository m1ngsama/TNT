#!/bin/bash
# Setup cron jobs for TNT maintenance

echo "Setting up TNT maintenance cron jobs..."

# Create scripts directory if it doesn't exist
mkdir -p /var/lib/tnt/scripts

# Copy scripts
cp "$(dirname "$0")/logrotate.sh" /var/lib/tnt/scripts/
cp "$(dirname "$0")/healthcheck.sh" /var/lib/tnt/scripts/
chmod +x /var/lib/tnt/scripts/*.sh

# Add cron jobs
CRON_FILE="/etc/cron.d/tnt"

cat > "$CRON_FILE" << 'EOF'
# TNT Chat Server Maintenance Tasks

# Log rotation - daily at 3 AM
0 3 * * * root /var/lib/tnt/scripts/logrotate.sh /var/lib/tnt/messages.log 100 10000 >> /var/log/tnt-logrotate.log 2>&1

# Health check - every 5 minutes
*/5 * * * * root /var/lib/tnt/scripts/healthcheck.sh 2222 5 >> /var/log/tnt-health.log 2>&1

EOF

chmod 644 "$CRON_FILE"

echo "Cron jobs installed:"
cat "$CRON_FILE"
echo ""
echo "Done! Maintenance tasks will run automatically."
echo "- Log rotation: Daily at 3 AM"
echo "- Health check: Every 5 minutes"
