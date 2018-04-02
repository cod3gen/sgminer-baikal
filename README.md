# sgminer-baikal
Rebuild of Baikal`s SG Miner for Giant X10 / N

Since the guys over at Baikal obviously have not made their version of SG Miner open source, lets do it for them!

The purpose for this recovery and rebuild was to see if it was possible to overclock these miners, and unlock extra algos for X10. Its not identical to the included sgminer with baikal miners however its performance is pretty damn close after monitoring two days. Some code cleanup should have been done, its messy thats its there without any proper use - and some of them are additional code from me thats not needed or even not correct.

I would like for the community to contribute, maybe we can find a solution for both overclocking and unlocking the extra algorithms, i also hope that this will make the guys over at baikal start to open source their software as according to SG Miner licensing, Bitmain already do this with their CG Miner!

libusb and jansson is included of which libusb seems to be modified so i had to recover it.

More information about SG Miner is better to look for at https://github.com/sgminer-dev/sgminer

# The Recovery
Currently recovered data from:

- PiZero_GX10_171229_V1.1.img
- PiZero_GN_180310_V1.0.img
- PiZero_GB_180105_V1.0.img
- Giant X10 Miner on hand
- Giant N Miner on hand

With a total of 687000 files recovered, of which about 601000 files where duplicates and removed. After some additional sorting i was left with 60223 files. These files can be found here:
https://drive.google.com/open?id=1ZSFova5RtgaxZD7K2PJdv-lI5BKC07KH

# Before Compiling
Depending on which miner you have, you need to edit the file "driver-baikal.h" and do changes to #define BAIKAL_CLK_DEF.


# How to install and compile?
- use SSH and log in as root user to your miner, default password is baikal.
- cd ~
- sudo apt-get install git
- git clone https://github.com/cod3gen/sgminer-baikal.git
- cd sgminer-baikal
- nano driver-baikal.h      DO CHANGES AS ABOVE. If you have Giant N, no need to do changes.
- autoreconf -fi && ./configure && make
- cp /opt/scripta/bin/sgminer /opt/scripta/bin/sgminer_ORG

This process will take about 15min. 

When its done you need to:
- screen -x sgminer
- ctrl+c to stop screen session
- cp sgminer /opt/scripta/bin/sgminer
Confirm overwrite. If you cannot overwrite, you need to stop screen session again and retry copy.

After a while you can reconnect to screen session(screen -x sgminer). When you do not need to see screen session any more, use ctrl+a+d keys to dettach!

# Known issues when compiling

If you run into an error such as "Killedg package lists… 99%", or other errors during compiling its because of low memory on the Pi. You need to add a swap which can be done as this:
- sudo mkdir -p /var/cache/swap
- sudo fallocate -l 256M /var/cache/swap/swap0
- sudo chmod 0600 /var/cache/swap/swap0
- sudo mkswap /var/cache/swap/swap0
- sudo swapon /var/cache/swap/swap0

# Included Softwares
- SG Miner information and licensing: https://github.com/sgminer-dev/sgminer
- LIB USB information and licensing: https://github.com/libusb/libusb
- Jansson information and licensing: https://github.com/akheron/jansson

# Get in touch
Get in touch at Discord - cod3gen#5466

Like my work?
- ETH: 0x57A9F99645dC74F9373409A8Ba18Bc0F92566af3
- LTC: LWEAPNb3FmVWcRewa5xu6mKtaQwwHvWZtX
- BTC: 3CVEThoqDY3RqS4fwnJWcUR6zd9Mfa2VLo
- XVG: DD3aZcGATCh55TvVTW4chg2PrUHLdV4u5k
