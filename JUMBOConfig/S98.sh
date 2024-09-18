#!/bin/bash

echo v0004

# Define the path where the USB is mounted and the destination paths
USB_MOUNT_PATH="/run/media/sda1/JBAI"
DEST_PATH_JSON="/home/petalinux/tyv3_dice/jfile.json"
DEST_PATH_XMODEL="/home/petalinux/tyv3_dice/model/tyv3_dice/tyv3_dice.xmodel"
DEST_PATH_PROTOTXT="/home/petalinux/tyv3_dice/model/tyv3_dice/tyv3_dice.prototxt"
DEST_PATH_LABELJ="/home/petalinux/tyv3_dice/model/tyv3_dice/label.json"
DEST_PATH_TXT="/home/petalinux/tyv3_dice/comport.txt"
DEST_PATH_INIT="/home/petalinux/tyv3_dice/initsetting.txt"
DEST_PATH_LIBAI="/home/petalinux/tyv3_dice/libivas_airender.so"
DEST_PATH_COMPORT="/home/petalinux/tyv3_dice/JBB0Comport"

# Check each file individually and copy if it exists
[[ -f "$USB_MOUNT_PATH/jfile.json" ]] && cp "$USB_MOUNT_PATH/jfile.json" "$DEST_PATH_JSON"
[[ -f "$USB_MOUNT_PATH/tyv3_dice.xmodel" ]] && cp "$USB_MOUNT_PATH/tyv3_dice.xmodel" "$DEST_PATH_XMODEL"
[[ -f "$USB_MOUNT_PATH/tyv3_dice.prototxt" ]] && cp "$USB_MOUNT_PATH/tyv3_dice.prototxt" "$DEST_PATH_PROTOTXT"
[[ -f "$USB_MOUNT_PATH/label.json" ]] && cp "$USB_MOUNT_PATH/label.json" "$DEST_PATH_LABELJ"
[[ -f "$USB_MOUNT_PATH/comport.txt" ]] && cp "$USB_MOUNT_PATH/comport.txt" "$DEST_PATH_TXT"
[[ -f "$USB_MOUNT_PATH/initsetting.txt" ]] && cp "$USB_MOUNT_PATH/initsetting.txt" "$DEST_PATH_INIT"
[[ -f "$USB_MOUNT_PATH/libivas_airender.so" ]] && cp "$USB_MOUNT_PATH/libivas_airender.so" "$DEST_PATH_LIBAI"
[[ -f "$USB_MOUNT_PATH/JBB0Comport" ]] && cp "$USB_MOUNT_PATH/JBB0Comport" "$DEST_PATH_COMPORT"

cd /home/petalinux/tyv3_dice

# Read the third line of initsetting.txt
if [ -f "$DEST_PATH_INIT" ]; then
    THIRD_LINE=$(sed -n '3p' "$DEST_PATH_INIT")
    if [ "$THIRD_LINE" = "0" ]; then
        exit 0
    fi
fi

# Read the second line of initsetting.txt for the IP address
if [ -f "$DEST_PATH_INIT" ]; then
    IP_ADDRESS=$(sed -n '2p' "$DEST_PATH_INIT")
    # Set the IP address (assuming you're using 'ifconfig', otherwise replace with the correct command)
    ifconfig eth0 $IP_ADDRESS
fi

sleep 5

# Check the first character of the first line in initsetting.txt
# Only proceed if initsetting.txt was successfully copied
if [ -f "$DEST_PATH_INIT" ]; then
    FIRST_CHAR=$(head -n 1 "$DEST_PATH_INIT")
    echo "$FIRST_CHAR"
    # Execute the script based on the first character
    if [ "$FIRST_CHAR" = "0" ]; then
        
        /home/petalinux/tyv3_dice/2_test_nodisplay.sh > /dev/null 2>&1 &
    else
        /home/petalinux/tyv3_dice/1_test.sh > /dev/null 2>&1 &
    fi
fi

sleep 10

# Execute the comport script if it exists
[[ -f "/home/petalinux/tyv3_dice/JBB0Comport" ]] && /home/petalinux/tyv3_dice/JBB0Comport 0 &
#[[ -f "/home/petalinux/tyv3_dice/JBB0Comport" ]] && sudo /home/petalinux/tyv3_dice/JBB0Comport 0 &

exit 0