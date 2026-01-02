// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2023 Hewlett-Packard Development Company, L.P.
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/gpio.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/i2c.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/err.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/poll.h>
#include <linux/debugfs.h>
#include <linux/bitops.h>
#include <linux/nvmem-consumer.h>


#define PINS_PER_DRIVE 3
#define GPIO_DIR_OUT 0
#define GPIO_DIR_IN  1

// EEPROM defintions

struct common_header {
	unsigned char hdrFormat; // 0x01
	unsigned char InternalUse; // 0x00 : unused
	unsigned char ChassisInfo; // 0x00 : unused
	unsigned char BoardArea; // 0x01
	unsigned char ProductInfo; // 0x05
	unsigned char MRArea; // 0x10
	unsigned char PAD;
	unsigned char checksum; 
};

struct board_info {
	unsigned char boardRevision;
	unsigned char boardInfoLength;
	unsigned char languageCode;
	unsigned char time[3];
	unsigned char manufactureHeader; // 0xC3
	unsigned char manufacturer[3];
	unsigned char productName;
	unsigned char sn;
	unsigned char pnHeader;
	unsigned char pn[10];
	unsigned char FRUFileID;
	unsigned char OEMRev;
	unsigned char OEMRecordId;
	unsigned char PCBRev[2];
	unsigned char endOfRecord;
	unsigned char pad[2];
	unsigned char checksum;
};

struct product_info {
	unsigned char productArea;
	unsigned char productInfoLength;
	unsigned char languageCode;
	unsigned char manufactureHeader; // 0xC3
        unsigned char manufacturer[256];
	unsigned char productNameHeader;
	unsigned char productName[256];
	unsigned char pnHeader;
	unsigned char pn[256];
	unsigned char versionHeader;
	unsigned char version[256];
	unsigned char snHeader;
	unsigned char sn[256];
	unsigned char assetTag;
	unsigned char FRUFileID;
	unsigned short int FRUFileID16bitBackplane;
	unsigned char FRUFileIDNVRAMVersion;
	unsigned char eor;
	unsigned char pad[5];
	unsigned char checksum;
};

struct UbmMR {
	unsigned char MRId;
	unsigned char eol;
	unsigned char recordLength;
	unsigned char recordChecksum;
	unsigned char headerChecksum;
	unsigned char specRev;
	unsigned char TwoWire;
	unsigned char TimeLimit;
	unsigned char Features[2];
	unsigned char DFCDesc;
	unsigned char PortRouteInfoDescCount;
	unsigned char DriveBayperBox;
	unsigned char MaxPowerPerBay;
	unsigned char MuxDesc;
	unsigned char reserverd;
};

struct UbmPortRoute {
	unsigned char MRId;
        unsigned char eol;
	unsigned char recordLength;
        unsigned char recordChecksum;
        unsigned char headerChecksum;
	unsigned char UBMPortRoute1[7];
	unsigned char UBMPortRoute2[7];
	unsigned char UBMPortRoute3[7];
	unsigned char UBMPortRoute4[7];
	unsigned char UBMPortRoute5[7];
	unsigned char UBMPortRoute6[7];
	unsigned char UBMPortRoute7[7];
	unsigned char UBMPortRoute8[7];
	unsigned char UBMPortRoute9[7];
	unsigned char UBMPortRoute10[7];
	unsigned char UBMPortRoute11[7];
	unsigned char UBMPortRoute12[7];
};

struct gxp_ubm_drvdata {
        struct i2c_client *client;
        u8 low;
        u8 high;
        struct mutex update_lock;
        struct device *hwmon_dev;
        struct dentry *debugfs;
        struct product_info *pinfo;
        struct gpio_chip gpio_chip;
	struct UbmMR *ubmmr;
	struct UbmPortRoute *ubmportroute;
};

enum ubm_gpio_pn {
	DRV_PRESENCE = 0,
	DRV_UUID,
	DRV_ACT
};


#define REG_11 0xb
#define REG_TEMP 0x31
#define HUB_REG(reg) ((u8) ~0x80 | reg)
#define SPD_SIZE 1024

struct mutex drv_lock;
unsigned int currentSpdIndex=0;


unsigned char spd[SPD_SIZE]; // store SPD
unsigned char enable=0; 
unsigned char enableBuf;

int spd_len = 0;

static unsigned char checksum(unsigned char *buffer, unsigned int length, unsigned char seed, unsigned char i2caddr);


static int gxp_gpio_ubm_get(struct gpio_chip *chip, unsigned int offset)
{
	int ret=0;
	char driveNumber;
	// We must get access to the drvdata
	struct gxp_ubm_drvdata *drvdata;
	char packetChecksum;
	int i;
	char dfc[256];


        char DFCSelectCommand[2] = { 0x36, 0x00 }; // by default we select drive 0
        char DFCReadCommand[1] = { 0x40 }; // by default we select drive 0

	drvdata = dev_get_drvdata(chip->parent);
	if ( drvdata != NULL )
		printk("gxp-ubm: DFC descriptors count %d", drvdata->ubmmr->DFCDesc);

	driveNumber = offset / PINS_PER_DRIVE;
	printk("gxp-ubm: Checking drive %d", driveNumber);
	
	switch (offset % PINS_PER_DRIVE) {
		case DRV_PRESENCE:
			// Ok we need to check if the drive is present
			// We need to run the DFC status for the specific drives
			// Usually that means set the MUX to the right value
			// and read DFC by issuing a 0x40 command read
			DFCSelectCommand[1] = driveNumber;
			packetChecksum = checksum(DFCSelectCommand,2, 0xa5, 0x80);
			for ( i=0; i<2; i++)
        		{       
				usleep_range(50, 150);
		                ret = i2c_smbus_write_byte(drvdata->client,DFCSelectCommand[i]);
		                if ( ret < 0 )
		                {
		                        printk("gxp-ubm: device not responding \n");
		                        return 0;
		                }
		        }
		        usleep_range(50, 150);
			ret = i2c_smbus_write_byte(drvdata->client,packetChecksum);
                        if ( ret < 0 )
                        {
				printk("gxp-ubm: device not responding \n");
                                return 0;
                        }
			// let's retreive the drive state now
			memset(&dfc[0],0,256);

			packetChecksum = checksum(DFCReadCommand,1,0xa5, 0x80);
		        usleep_range(50, 150);
			ret = i2c_smbus_write_byte(drvdata->client,DFCReadCommand[0]);
			printk("gxp-ubm: checksum %02x", packetChecksum);
                        if ( ret < 0 )
                        {
				printk("gxp-ubm: device not responding \n");
                                return 0;
                        }
			ret = i2c_smbus_write_byte(drvdata->client,packetChecksum);
                        if ( ret < 0 )
                        {
                                printk("gxp-ubm: device not responding \n");
                                return 0;
                        }
			for ( i = 0 ; i < 8 ; i++ )
			{
			        usleep_range(50, 150);
				dfc[i] = i2c_smbus_read_byte(drvdata->client);
			}
			i2c_smbus_read_i2c_block_data(
                        printk("%x %x %x %x %x %x %x %x\n", dfc[0], dfc[1],dfc[2],dfc[3],dfc[4],dfc[5],dfc[6],dfc[7]);
		       break;
		default:
			break;	       
	}
	return ret;
}

static void gxp_gpio_ubm_set(struct gpio_chip *chip,
                        unsigned int offset, int value)
{
}

static int gxp_gpio_ubm_get_direction(struct gpio_chip *chip, unsigned int offset)
{
	int ret = GPIO_DIR_IN;
        switch (offset % PINS_PER_DRIVE) {
        case DRV_UUID ... DRV_ACT:
                ret = GPIO_DIR_OUT;
                break;
        default:
                break;
        }
	return ret;
}

static int gxp_gpio_ubm_direction_input(struct gpio_chip *chip,
                                unsigned int offset)
{
        int ret = 0;
	switch (offset % PINS_PER_DRIVE) {
        case DRV_UUID ... DRV_ACT:
                ret = -ENOTSUPP;
                break;
        default:
                break;
        }
        return ret;
}

static int gxp_gpio_ubm_direction_output(struct gpio_chip *chip,
                                unsigned int offset, int value)
{
	int ret = -ENOTSUPP;
	switch (offset % PINS_PER_DRIVE) {
        case DRV_UUID ... DRV_ACT:
		gxp_gpio_ubm_set(chip, offset, value);
		ret=0;
		break;
	default:
		break;
	}
	return ret;
}

static struct gpio_chip ubm_chip = {
        .label                  = "gxp-ubm-", // Need to add to that string the i2c mapping as we will get multiple chips
        .owner                  = THIS_MODULE,
        .get                    = gxp_gpio_ubm_get,
        .set                    = gxp_gpio_ubm_set,
        .get_direction = gxp_gpio_ubm_get_direction,
        .direction_input = gxp_gpio_ubm_direction_input,
        .direction_output = gxp_gpio_ubm_direction_output,
        .base = -1,
        //.can_sleep            = true,
};


static ssize_t spd_read(struct file *filp, struct kobject *kobj,
                           struct bin_attribute *bin_attr,
                           char *buf, loff_t off, size_t count);

static const struct bin_attribute spd_attr = {
        .attr = {
                .name = "spd",
                .mode = 0444,
        },
        .size = SPD_SIZE,
        .read = spd_read,
};

static ssize_t device_enable(struct file *filp,struct kobject *kobj,
				   struct bin_attribute *bin_attr,
				   char *buffer, loff_t offset, size_t count);

static const struct bin_attribute device_attr = {
        .attr = {
                .name = "enable",
                .mode = 0666,
        },
        .size = 1,
        .write = device_enable,
};

static void gxp_ubm_fs_init(struct gxp_ubm_drvdata *drvdata)
{
/* Create the sysfs eeprom file */
	int err=0;
//        err = sysfs_create_bin_file(&drvdata->client->dev.kobj, &device_attr);
}

static ssize_t device_enable(struct file *filp,struct kobject *kobj,
                                   struct bin_attribute *bin_attr,
                                   char *buffer, loff_t offset, size_t count)
{
	printk("gxp-ubm: buffer %x\n", *buffer);
	return(0);
}

static ssize_t spd_read(struct file *filp, struct kobject *kobj,
                           struct bin_attribute *bin_attr,
                           char *buf, loff_t off, size_t count)
{
	int currentIndex=0;
	currentSpdIndex = off;
	while(( currentIndex < count) && (currentSpdIndex != SPD_SIZE))
	{
			buf[currentIndex] = (unsigned char) spd[currentSpdIndex];
			currentIndex++;
			currentSpdIndex++;
	}
	return currentIndex;
}


static int gxp_ubm_update_client(struct device *dev, u8 reg)
{
	struct gxp_ubm_drvdata *drvdata = dev_get_drvdata(dev);
	u16 ret = 0;
	switch (reg) {
	default:
		dev_err(&drvdata->client->dev, "gxp_ubm_error_reg 0x%x unknown\n", reg);
		return -EOPNOTSUPP;
	}

	return ret;
}

static ssize_t show_ubm_temp(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct gxp_ubm_drvdata *drvdata = dev_get_drvdata(dev);
	int ret = 0;
	ret = gxp_ubm_update_client(dev, REG_TEMP);
	if (ret < 0)
		return ret;
	return 0;
}

static ssize_t show_ubm_label(struct device *dev, struct device_attribute *attr,
                                char *buf)
{
        return sprintf(buf, "ubm\n");
}


static SENSOR_DEVICE_ATTR(temp1_input, 0444, show_ubm_temp, NULL, 0);

static struct attribute *gxp_ubm_attrs[] = {
	&sensor_dev_attr_temp1_input.dev_attr.attr,
	NULL,
};

ATTRIBUTE_GROUPS(gxp_ubm);

static const struct of_device_id gxp_ubm_of_match[] = {
        { .compatible = "hpe,gxp-ubm" },
        {},
};
MODULE_DEVICE_TABLE(of, gxp_ubm_of_match);

static int gxp_ubm_remove(struct i2c_client *client)
                
{
//	sysfs_remove_bin_file(&client->dev.kobj, &spd_attr);
	struct gxp_ubm_drvdata *drvdata = i2c_get_clientdata(client);
	printk("gxp-ubm: unregistering %s:%x\n", client->adapter->name, client->addr);
//	sysfs_remove_bin_file(&client->dev.kobj, &device_attr);
	hwmon_device_unregister(&client->dev);
	printk("drvdata %lx\n", drvdata);
	if ( drvdata != NULL )
		if ( drvdata->gpio_chip.gpiodev != NULL )
			gpiochip_remove(&drvdata->gpio_chip);
	return 0;
}

static int gxp_ubm_detect(struct i2c_client *client,
                         struct i2c_board_info *info)
{
	printk("gxp-ubm: detect %x\n", client->addr);
	return -ENODEV;
}

static unsigned char checksum(unsigned char *buffer, unsigned int length, unsigned char seed, unsigned char i2caddr)
{
	// Checksum formula is
	// 0x100 - (0xa5 + 0x80 + sum(buffer) & 0xff)
	// 0xa5 checksum seed
	// 0x80 is the 8-bit address of the UBM on i2c bus (0x40 in 7-bit mode)
	unsigned char sum;
	int i;
	sum=(unsigned char)(seed)+(unsigned char)(i2caddr);
	for ( i=0 ; i < length ; i++)
		sum += buffer[i];
	sum = sum & 0xff;
	sum = 0x100 - sum;
        printk("gxp-ubm: checksum %02x", sum);
	return sum;
}

static int gxp_ubm_probe(struct i2c_client *client)
{
	struct gxp_ubm_drvdata *drvdata;
	struct device *hwmon_dev;
	int i,j,index;
	int ret;
	char InitCommand[3] = { 0x34, 0xbf, 0x00 };
	unsigned char packetChecksum;
	struct device_node *np;
	struct platform_device *pdev;
	struct i2c_client *eepromclient;
	struct nvmem_device *nvmem_device;
	unsigned char eeprom[256];
	unsigned char buf2[256];
	char display[1024];
	static char gpioLabel[256];
	int maxGpioLabel;
	int bytes;
	int checked;
	unsigned char headerchecksum, boardinfochecksum, productinfochecksum;
	unsigned char ubmrchecksum1, ubmrchecksum2, ubmportroutechecksum1, ubmportroutechecksum2;
	struct common_header *header;

        unsigned char BoardInfoOffset, ProductInfoOffset, MROffset, UPROffset;
	unsigned char currentChecksum;

	struct board_info *boardinfo;
	struct product_info *productinfo;
	struct UbmMR *ubmmr;
	struct UbmPortRoute *ubmportroute;


	struct product_info *pinfo;

	char *ptr;


	char *eepromNamePtr;
	int len,datalength;


	printk("gxp-ubm: probing %s:%x\n", client->adapter->name,client->addr);

	if (!i2c_check_functionality(client->adapter,
				I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL)) {
		return -EIO;
	}

	drvdata = devm_kzalloc(&client->dev, sizeof(struct gxp_ubm_drvdata),
			GFP_KERNEL);
	if (!drvdata)
		return -ENOMEM;

	drvdata->pinfo = devm_kzalloc(&client->dev, sizeof(struct product_info),
			GFP_KERNEL);
	if (!drvdata->pinfo)
                return -ENOMEM;

	drvdata->ubmmr = devm_kzalloc(&client->dev, sizeof(struct UbmMR),
                        GFP_KERNEL);
        if (!drvdata->ubmmr)
                return -ENOMEM;

	drvdata->ubmportroute = devm_kzalloc(&client->dev, sizeof(struct UbmPortRoute),
                        GFP_KERNEL);
        if (!drvdata->ubmportroute)
                return -ENOMEM;

	pinfo = drvdata->pinfo;

	drvdata->client = client;
	i2c_set_clientdata(client, drvdata);

	mutex_init(&drvdata->update_lock);
	mutex_init(&drv_lock);

	drvdata->hwmon_dev = NULL;

	// We have to init the UBM
        // to do that we initiate 4 writes 0x34 0xbf 0x00 0xe8
        packetChecksum = checksum(InitCommand,3, 0xa5, 0x80);

        for ( i=0; i<3; i++)
        {
		usleep_range(50, 150);
                ret = i2c_smbus_write_byte(drvdata->client,InitCommand[i]);
		// printk("gxp-ubm: %02x", InitCommand[i]);
                if ( ret < 0 )
                {
                        printk("gxp-ubm: device not present aborting \n");
                        return -ENODEV;
                }
        }

	usleep_range(50, 150);
        ret = i2c_smbus_write_byte(drvdata->client,packetChecksum);
	printk("gxp-ubm: %02x", packetChecksum);
        if ( ret < 0 )
        {
                printk("gxp-ubm: device not present aborting \n");
                return -ENODEV;
        }

	// Let's try to find the eeprom handle first
	pdev=to_platform_device(&client->dev);
	np = of_parse_phandle((&client->dev)->of_node, "eeprom_phandle", 0);
	if ( !np )
	       printk("gxp-ubm: Missing eeprom phandle\n");	
	else
	{
		eepromclient=of_find_i2c_device_by_node(np);
		if ( !eepromclient )
			printk("gpx-ubm: can't access eeprom_phandle: %s %s\n",np->name,
					 np->full_name);
		else
		{
			printk("gxp-ubm: eeprom_phandle setup\n");
			// Checking eeprom access through nvmem API
			// The name is associated with the current client address
			eepromNamePtr=of_get_property(np, "label", &len); 
			printk("gxp-ubm: label %s %d", eepromNamePtr, len);

			nvmem_device=nvmem_device_get(&eepromclient->dev, "piceeprom1");
			if ( nvmem_device < 0 )
			{
				printk("gxp-ubm: can't access eeprom\n");
				goto err;
			}
			else
			{
				bytes=nvmem_device_read(nvmem_device,0,256,&eeprom);
				printk("gxp-ubm: eeprom successfully read %d\n", bytes);
				for ( i = 0 ; i < 256; i++ )
				{
					sprintf(&buf2[3*(i%16)],"%02x ",eeprom[i]);
			                if ( i%16 == 15 )
                        			printk("%s\n",buf2);
				}
				header = ( struct common_header * ) eeprom;

				BoardInfoOffset = eeprom[3]*8;
			        ProductInfoOffset = eeprom[4]*8;
			        MROffset = eeprom[5]*8;

				headerchecksum=checksum((char *)header,7, 0,0);
				currentChecksum = eeprom[7];

				if ( headerchecksum != currentChecksum )
				{
					printk("gxp-ubm: checksum computation error on eeprom ");
					goto err;
				}
				boardinfo = ( struct board_info *)(&eeprom[eeprom[3]*8]);

				currentChecksum = eeprom[BoardInfoOffset+eeprom[BoardInfoOffset+1]*8-1];
				boardinfochecksum =checksum((char *)(&eeprom[BoardInfoOffset]), (eeprom[BoardInfoOffset+1]*8)-1,0,0);

				//if ( boardinfochecksum != boardinfo->checksum )
				if ( boardinfochecksum != currentChecksum )
                                {
                                        printk("gxp-ubm: board checksum computation error on eeprom ");
					goto err;
                                }

				currentChecksum = eeprom[ProductInfoOffset+eeprom[ProductInfoOffset+1]*8-1];

                                productinfochecksum =checksum((char *)(&eeprom[ProductInfoOffset]), (eeprom[ProductInfoOffset+1]*8)-1, 0 , 0);

                                if ( productinfochecksum != currentChecksum )
                                {
                                        printk("gxp-ubm: product info checksum computation error on eeprom ");
					goto err;
                                }

				pinfo->languageCode = eeprom[ProductInfoOffset+2];
			        datalength = eeprom[ ProductInfoOffset + 3];
			        memset(&pinfo->manufacturer[0],0,256);
       				strncpy( &pinfo->manufacturer[0], &eeprom[ ProductInfoOffset + 4], datalength & 0x3F);
				printk("gxp-ubm: Manufacturer %s", pinfo->manufacturer);
				
				ProductInfoOffset += ( datalength & 0x3F ) + 4;

			        datalength = eeprom[ ProductInfoOffset ];
			        memset(&pinfo->productName[0],0,256);
			        strncpy(&pinfo->productName[0], &eeprom[ ProductInfoOffset + 1], datalength & 0x3F);
				printk("gxp-ubm: Product Name %s", pinfo->productName);

				ProductInfoOffset += ( datalength & 0x3F ) + 1;

        			datalength = eeprom[ ProductInfoOffset ];
			        memset(&pinfo->pn[0],0,256);
			        strncpy(&pinfo->pn[0], &eeprom[ ProductInfoOffset + 1], datalength & 0x3F);
				printk("gxp-ubm: P/N %s", pinfo->pn);

			        ProductInfoOffset += ( datalength & 0x3F ) + 1;

			        datalength = eeprom[ ProductInfoOffset ];
			        memset(&pinfo->version[0],0,256);
			        strncpy( &pinfo->version[0], &eeprom[ ProductInfoOffset + 1], datalength & 0x3F);
				printk("gxp-ubm: version %s",pinfo->version);

			        ProductInfoOffset += ( datalength & 0x3F ) + 1;

			        datalength = eeprom[ ProductInfoOffset ];
			        memset(&pinfo->sn[0],0,256);
			        strncpy(&pinfo->sn[0], &eeprom[ ProductInfoOffset + 1], datalength & 0x3F);

			        ProductInfoOffset += ( datalength & 0x3F ) + 1;
			        pinfo->FRUFileID = eeprom[ ProductInfoOffset ];
			        if ( ( pinfo->FRUFileID & 0x3F) == 0x03 )
			        {
			                pinfo->FRUFileID16bitBackplane=(short int)eeprom[ ProductInfoOffset + 1 ];
			                pinfo->FRUFileIDNVRAMVersion=eeprom[ ProductInfoOffset + 3];
			        }
			        else
			        {

			                pinfo->FRUFileID16bitBackplane=0;
			                pinfo->FRUFileIDNVRAMVersion=0;
			        }


//				ubmmr = ( struct UbmMR * )(&eeprom[MROffset]);
				ubmmr = drvdata->ubmmr;
				currentChecksum = eeprom[MROffset+3];
				ubmrchecksum1 = checksum((char *)&eeprom[MROffset+5],eeprom[MROffset+2]-1 , 0,0);
				if ( ubmrchecksum1 != currentChecksum )
				{
					printk("gxp-ubm: ubmr record checksum issue on eeprom \n");
					goto err;
				}
				ubmrchecksum2 = checksum((char *)&eeprom[MROffset],4, 0,0); 
				currentChecksum = eeprom[MROffset+4];
				if ( ubmrchecksum2 != currentChecksum )
				{
					printk("gxp-ubm: ubmr header checksum issue on eeprom \n");
					goto err;
				}

				ubmmr->recordLength = eeprom[ MROffset + 2 ];
        			ubmmr->specRev = eeprom[ MROffset + 5 ];
			        ubmmr->TwoWire = eeprom[ MROffset + 6 ];
			        ubmmr->TimeLimit = eeprom[ MROffset + 7 ];
			        ubmmr->DFCDesc = eeprom[ MROffset + 10 ];
			        ubmmr->PortRouteInfoDescCount = eeprom[ MROffset + 11 ];
			        ubmmr->DriveBayperBox = eeprom[ MROffset + 12 ];
			        ubmmr->MaxPowerPerBay = eeprom[ MROffset + 13];
			        ubmmr->MuxDesc = eeprom[ MROffset + 14];


				// the start of ubmportroute is computed from the end of the previous packet

				ubmportroute = ( struct UbmPortRoute *)(&eeprom[MROffset + 5 + ubmmr->recordLength]);
				ubmportroutechecksum1 = checksum((char *) ubmportroute + 5 , 83, 0,0);
				if ( ubmportroutechecksum1 != ubmportroute->recordChecksum )
                                {
                                        printk("gxp-ubm: ubm port route record checksum issue on eeprom \n");
					goto err;
                                }
				ubmportroutechecksum2 = checksum((char *) ubmportroute, 4,0,0);
                                if ( ubmportroutechecksum2 != ubmportroute->headerChecksum )
                                {
                                        printk("gxp-ubm: ubm port route  header checksum issue on eeprom \n");
					goto err;
                                }

				// Ok we have read the FRU
			        // let's print how many drive per bay can be accepted
				printk("gxp-ubm: eeprom validated");
			        printk("gxp-ubm: UBM EEPROM config\n %d drives per bay\n %d Power per Drive\n %d Mux channel count",
		                        ubmmr->DriveBayperBox, ubmmr->MaxPowerPerBay, ubmmr->MuxDesc & 0x3);
				printk("gxp-ubm: DFC descriptors count %d", ubmmr->DFCDesc);
				printk("gxp-ubm: init done");

				UPROffset = MROffset + 5 + ubmmr->recordLength;
				ubmportroute = drvdata->ubmportroute;
				ubmportroute->MRId = eeprom[UPROffset];
			        ubmportroute->eol = eeprom[UPROffset + 1];
			        ubmportroute->recordLength = eeprom[UPROffset + 2];
			        memset(ubmportroute->UBMPortRoute1, 0, 7 );
			        memset(ubmportroute->UBMPortRoute2, 0, 7 );
			        memset(ubmportroute->UBMPortRoute3, 0, 7 );
			        memset(ubmportroute->UBMPortRoute4, 0, 7 );
			        memset(ubmportroute->UBMPortRoute5, 0, 7 );
			        memset(ubmportroute->UBMPortRoute6, 0, 7 );
			        memset(ubmportroute->UBMPortRoute7, 0, 7 );
			        memset(ubmportroute->UBMPortRoute8, 0, 7 );
			        memset(ubmportroute->UBMPortRoute9, 0, 7 );
			        memset(ubmportroute->UBMPortRoute10, 0, 7 );
			        memset(ubmportroute->UBMPortRoute11, 0, 7 );
			        memset(ubmportroute->UBMPortRoute12, 0, 7 );
				ptr=ubmportroute->UBMPortRoute1;
        			for ( i = 0 ; i < 12; i++ )
			        {
			                memcpy(ptr,  &eeprom[UPROffset + 5 + 7*i], 7);
			                ptr+=7;
        			}

				// ok now we can create the GPIO chip
				memset(gpioLabel,0,256);
				strncpy(gpioLabel,"gxp-ubm-",8);
				strncpy(&gpioLabel[8],eepromNamePtr,len);

			        drvdata->gpio_chip = ubm_chip;
				drvdata->gpio_chip.label = gpioLabel;
        			drvdata->gpio_chip.ngpio = 3*ubmmr->DriveBayperBox;
			        drvdata->gpio_chip.parent = &pdev->dev;
			        printk("Max GPIOS %d\n", ARCH_NR_GPIOS);
			        // ret = devm_gpiochip_add_data(&pdev->dev, &drvdata->gpio_chip, NULL);
				ret=gpiochip_add(&drvdata->gpio_chip);
		                // gpiochip_remove(&drvdata->gpio_chip);
			        // ret = devm_gpiochip_add_data(&client->dev, &drvdata->gpio_chip, NULL);
			        if (ret < 0)
			                dev_err(&pdev->dev, "Could not register gpiochip for ubm, %d\n", ret);
			}

		}
	}

	mutex_lock(&drv_lock);
	hwmon_dev = devm_hwmon_device_register_with_groups(&client->dev, "HPEubm",
			drvdata, gxp_ubm_groups);
	if (IS_ERR(hwmon_dev))
		return PTR_ERR(hwmon_dev);
	drvdata->hwmon_dev = hwmon_dev;
	mutex_unlock(&drv_lock);
//	gxp_ubm_fs_init(drvdata);
	return 0;
err:
	return -ENODEV;
}

static struct i2c_driver gxp_ubm_driver = {
	.class = I2C_CLASS_HWMON,
	.probe_new	= gxp_ubm_probe,
	.remove		= gxp_ubm_remove,
	.detect 	= gxp_ubm_detect, 
	.driver = {
		.name	= "gxp-ubm",
		.of_match_table = gxp_ubm_of_match,
	},
};
module_i2c_driver(gxp_ubm_driver);

MODULE_AUTHOR("Jean-Marie Verdun <verdun@hpe.com>");
MODULE_DESCRIPTION("HPE GXP UBM driver");
MODULE_LICENSE("GPL");
