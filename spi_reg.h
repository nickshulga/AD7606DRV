/*
    SPI access on RP1 through PCI BAR1
*/
#pragma once
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/types.h>



////////////////////////////////////////////////////////////////

// pci bar info
// from: https://github.com/G33KatWork/RP1-Reverse-Engineering/blob/master/pcie/hacks.py
#define RP1_BAR1 0x1f00000000
#define RP1_BAR1_LEN 0x400000

// offsets from include/dt-bindings/mfd/rp1.h
// https://github.com/raspberrypi/linux/blob/rpi-6.1.y/include/dt-bindings/mfd/rp1.h
#define RP1_IO_BANK0_BASE 0x0d0000
#define RP1_RIO0_BASE 0x0e0000
#define RP1_PADS_BANK0_BASE 0x0f0000

// the following info is from the RP1 datasheet (draft & incomplete as of 2024-02-18)
// https://datasheets.raspberrypi.com/rp1/rp1-peripherals.pdf
#define RP1_ATOM_XOR_OFFSET 0x1000
#define RP1_ATOM_SET_OFFSET 0x2000
#define RP1_ATOM_CLR_OFFSET 0x3000

#define PADS_BANK0_VOLTAGE_SELECT_OFFSET 0
#define PADS_BANK0_GPIO_OFFSET 0x4

#define RIO_OUT_OFFSET 0x00
#define RIO_OE_OFFSET 0x04
#define RIO_NOSYNC_IN_OFFSET 0x08
#define RIO_SYNC_IN_OFFSET 0x0C
//                           3         2         1
//                          10987654321098765432109876543210
#define CTRL_MASK_FUNCSEL 0b00000000000000000000000000011111
#define PADS_MASK_OUTPUT  0b00000000000000000000000011000000

#define CTRL_FUNCSEL_RIO 0x05



////////////////////////////////////////////////////
// SPI
#define RP1_SPI8_BASE 0x04c000  // not available on gpio
#define RP1_SPI0_BASE 0x050000
#define RP1_SPI1_BASE 0x054000
#define RP1_SPI2_BASE 0x058000
#define RP1_SPI3_BASE 0x05c000
#define RP1_SPI4_BASE 0x060000
#define RP1_SPI5_BASE 0x064000
#define RP1_SPI6_BASE 0x068000  // not available on gpio
#define RP1_SPI7_BASE 0x06c000  // not available on gpio

typedef struct
{
    uint8_t number;
    volatile uint32_t *status;
    volatile uint32_t *ctrl;
    volatile uint32_t *pad;
} gpio_pin_t;

typedef struct {

    volatile void *regbase;
    char *txdata;
    char *rxdata;
    uint8_t txcount;

} rp1_spi_instance_t;

typedef struct
{
    volatile void *rp1_peripherial_base;
    volatile void *gpio_base;
    volatile void *pads_base;
    volatile uint32_t *rio_out;
    volatile uint32_t *rio_output_enable;
    volatile uint32_t *rio_nosync_in;

    gpio_pin_t *pins[27];
    rp1_spi_instance_t *spis[6];

} rp1_t;

////////////////////////////////////////////////////////////////
//                S P I   R E G I S T E R
///////////////////////////////////////////////////////////////
// from: https://github.com/raspberrypi/linux/blob/rpi-6.1.y/drivers/spi/spi-dw.h

/* Register offsets (Generic for both DWC APB SSI and DWC SSI IP-cores) */
// many of the descriptions below were auto-filled by co-pilot, so they may not be accurate
// second comments are second suggestions by co-pilot
#define DW_SPI_CTRLR0 0x00 // control register 0 (frame format, clock polarity, phase, etc.) 
#define DW_SPI_CTRLR1 0x04 // control register 1 (number of data frames)
#define DW_SPI_SSIENR 0x08 // enable register (enable/disable the SPI controller)
#define DW_SPI_MWCR 0x0c // Microwire control register (not used in SPI mode)
#define DW_SPI_SER 0x10     // chip select, bit = chip select line? see dw_spi_set_cs()   // slave enable register
#define DW_SPI_BAUDR 0x14 // baud rate register // author: verified on the Pi5 to be a straight divisor of 200MHz clk_sys
#define DW_SPI_TXFTLR 0x18 // seems to be the number of bytes in the TX FIFO // transmit FIFO threshold level
#define DW_SPI_RXFTLR 0x1c // seems to be the number of bytes in the RX FIFO // receive FIFO threshold level
#define DW_SPI_TXFLR 0x20   // seems to be the number of bytes in the TX FIFO // transmit FIFO level
#define DW_SPI_RXFLR 0x24  // seems to be the number of bytes in the RX FIFO // receive FIFO level
#define DW_SPI_SR 0x28     // status register
#define DW_SPI_IMR 0x2c   // interrupt mask register
#define DW_SPI_ISR 0x30  // interrupt status register
#define DW_SPI_RISR 0x34  // raw interrupt status register 
#define DW_SPI_TXOICR 0x38 // transmit FIFO overflow interrupt clear register
#define DW_SPI_RXOICR 0x3c // receive FIFO overflow interrupt clear register
#define DW_SPI_RXUICR 0x40 // receive FIFO underflow interrupt clear register
#define DW_SPI_MSTICR 0x44  // multi-master interrupt clear register
#define DW_SPI_ICR 0x48 // interrupt clear register
#define DW_SPI_DMACR 0x4c // DMA control register
#define DW_SPI_DMATDLR 0x50 // DMA transmit data level register
#define DW_SPI_DMARDLR 0x54 // DMA receive data level register
#define DW_SPI_IDR 0x58 // Identification register
#define DW_SPI_VERSION 0x5c     // version register
#define DW_SPI_DR 0x60 // data register
#define DW_SPI_RX_SAMPLE_DLY 0xf0   // receive sample delay register
#define DW_SPI_CS_OVERRIDE 0xf4 // chip select override register

//  Each SSI controller is based on a configuration of the Synopsys DW_apb_ssi IP (v4.02a).
// APB
/* Bit fields in CTRLR0 (DWC APB SSI) */
//                                     3         2         1
//                                    10987654321098765432109876543210

#define DW_PSSI_CTRLR0_DFS_MASK     0b00000000000000000000000000001111 //GENMASK(3, 0)
#define DW_PSSI_CTRLR0_DFS32_MASK   0b00000000000111110000000000000000 //GENMASK(20, 16)

#define DW_PSSI_CTRLR0_FRF_MASK     0b00000000000000000000000000110000 //GENMASK(5, 4)
#define DW_SPI_CTRLR0_FRF_MOTO_SPI 0x0
#define DW_SPI_CTRLR0_FRF_TI_SSP 0x1
#define DW_SPI_CTRLR0_FRF_NS_MICROWIRE 0x2
#define DW_SPI_CTRLR0_FRF_RESV 0x3

#define DW_PSSI_CTRLR0_MODE_MASK    0b00000000000000000000000011000000 //GENMASK(7, 6)
#define DW_PSSI_CTRLR0_SCPHA        0b00000000000000000000000001000000 // BIT(6)
#define DW_PSSI_CTRLR0_SCPOL        0b00000000000000000000000010000000 //BIT(7)

#define DW_PSSI_CTRLR0_TMOD_MASK    0b00000000000000000000001100000000// GENMASK(9, 8)
#define DW_SPI_CTRLR0_TMOD_TR 0x0        /* xmit & recv */
#define DW_SPI_CTRLR0_TMOD_TO 0x1        /* xmit only */
#define DW_SPI_CTRLR0_TMOD_RO 0x2        /* recv only */
#define DW_SPI_CTRLR0_TMOD_EPROMREAD 0x3 /* eeprom read mode */

#define DW_PSSI_CTRLR0_SLV_OE       0b00000000000000000000010000000000 // BIT(10)
#define DW_PSSI_CTRLR0_SRL          0b00000000000000000000100000000000 // BIT(11)
#define DW_PSSI_CTRLR0_CFS          0b00000000000000000001000000000000 // BIT(12)

/* Bit fields in CTRLR0 (DWC SSI with AHB interface) - not relevant in the RP1 */
#define DW_HSSI_CTRLR0_DFS_MASK     0b00000000000000000000000000011111 // GENMASK(4, 0)
#define DW_HSSI_CTRLR0_FRF_MASK     0b00000000000000000000000011000000 // GENMASK(7, 6)
#define DW_HSSI_CTRLR0_SCPHA        0b00000000000000000000000100000000 // BIT(8)
#define DW_HSSI_CTRLR0_SCPOL        0b00000000000000000000001000000000 // BIT(9)
#define DW_HSSI_CTRLR0_TMOD_MASK    0b00000000000000000000011000000000 // GENMASK(11, 10)
#define DW_HSSI_CTRLR0_SRL          0b00000000000000000001000000000000 // BIT(13)
#define DW_HSSI_CTRLR0_MST          0b10000000000000000000000000000000 // BIT(31)

/* Bit fields in CTRLR1 */
#define DW_SPI_NDF_MASK             0b00000000000000001111111111111111 // GENMASK(15, 0) // Number of data frames

/* Bit fields in SR, 7 bits */
#define DW_SPI_SR_MASK              0b00000000000000000000000001111111 // GENMASK(6, 0)
#define DW_SPI_SR_BUSY              0b00000000000000000000000000000001 // BIT(0)
#define DW_SPI_SR_TF_NOT_FULL       0b00000000000000000000000000000010 // BIT(1)
#define DW_SPI_SR_TF_EMPT           0b00000000000000000000000000000100 // BIT(2)
#define DW_SPI_SR_RF_NOT_EMPT       0b00000000000000000000000000001000 // BIT(3)
#define DW_SPI_SR_RF_FULL           0b00000000000000000000000000010000 // BIT(4)
#define DW_SPI_SR_TX_ERR            0b00000000000000000000000000100000 // BIT(5)
#define DW_SPI_SR_DCOL              0b00000000000000000000000001000000 // BIT(6)

/* Bit fields in ISR, IMR, RISR, 7 bits */
#define DW_SPI_INT_MASK             0b00000000000000000000000000111111 // GENMASK(5, 0)
#define DW_SPI_INT_TXEI             0b00000000000000000000000000000001 // BIT(0) TX FIFO empty
#define DW_SPI_INT_TXOI             0b00000000000000000000000000000010 // BIT(1) TX FIFO overflow
#define DW_SPI_INT_RXUI             0b00000000000000000000000000000100 // BIT(2) RX FIFO underflow
#define DW_SPI_INT_RXOI             0b00000000000000000000000000001000 // BIT(3) RX FIFO overflow
#define DW_SPI_INT_RXFI             0b00000000000000000000000000010000 // BIT(4) RX FIFO full
#define DW_SPI_INT_MSTI             0b00000000000000000000000000100000 // BIT(5) Multi-Master contention

/* Bit fields in DMACR */
#define DW_SPI_DMACR_RDMAE          0b00000000000000000000000000000001 // BIT(0)
#define DW_SPI_DMACR_TDMAE          0b00000000000000000000000000000010 // BIT(1)
//                                    10987654321098765432109876543210
//                                     3         2         1



///////////////////////////////////////////////////////////////
typedef enum {
    SPI_OK = 0,
    SPI_ERROR = 1,
    SPI_BUSY = 2,
    SPI_TIMEOUT = 3,
    SPI_INVALID = 4
} spi_status_t;

///////////////////////////////////////////////////////////////////////////////////////////////////////////
static rp1_t *rp1;
static rp1_spi_instance_t *spireg;
static void *peripheral_base_address;
void spi_close(void);
bool rp1_spi_create(rp1_t *rp1, uint8_t spinum, rp1_spi_instance_t **spi);
spi_status_t rp1_spi_purge_rx_fifo(rp1_spi_instance_t *spi, int* dwordspurged);
void *mapgpio(off_t dev_base, off_t dev_size);
bool create_rp1(rp1_t **rp1, void *base);
bool create_pin_2(uint8_t pinnumber, rp1_t *rp1, uint32_t funcmask);
bool create_pin(uint8_t pinnumber, rp1_t *rp1);
int pin_enable_output(uint8_t pinnumber, rp1_t *rp1);
void pin_on(rp1_t *rp1, uint8_t pin);
void pin_off(rp1_t *rp1, uint8_t pin);
void setup_spi_pins(rp1_t *rp1);
void my_spi_read(rp1_spi_instance_t *spi, uint8_t *data);
void spi_init(void);
void spi_close(void);
///////////////////////////////////////////////////////////////////////////////////////////////////////////

const uint32_t spi_bases[] = {
    RP1_SPI0_BASE,
    RP1_SPI1_BASE,
    RP1_SPI2_BASE,
    RP1_SPI3_BASE,
    RP1_SPI4_BASE,
    RP1_SPI5_BASE,
    RP1_SPI6_BASE,
    RP1_SPI7_BASE,
    RP1_SPI8_BASE
};
//-----------------------------------------------------------------------------------------------
bool rp1_spi_create(rp1_t *rp1, uint8_t spinum, rp1_spi_instance_t **spi)
{
    rp1_spi_instance_t *s = (rp1_spi_instance_t *)kzalloc(sizeof(rp1_spi_instance_t), GFP_KERNEL);    
    if (s == NULL) return false;

    s->regbase = rp1->rp1_peripherial_base + spi_bases[spinum];
    s->txdata = (char *)0x0;
    s->rxdata = (char *)0x0;
    s->txcount = 0x0;
    *spi = s;
    return true;
}
//--------------------------------------------------------------------------------------------------
spi_status_t rp1_spi_purge_rx_fifo(rp1_spi_instance_t *spi, int* dwordspurged)
{
    if (spi->txcount != 0) return SPI_BUSY;
    int readcount = 0;
    uint32_t temp;

    // read the remaining dwords from the buffer
    while((*(volatile uint32_t *)(spi->regbase + DW_SPI_SR)) & DW_SPI_SR_RF_NOT_EMPT)
    {
        // check if there is data to read (check status register for Read Fifo Not Empty)
        temp = *(volatile uint32_t *)(spi->regbase + DW_SPI_DR);
        readcount++;
    }

    *dwordspurged = readcount;
    return SPI_OK;
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////
void *mapgpio(off_t dev_base, off_t dev_size)
{
  void *mapped;

    mapped = ioremap(dev_base, dev_size);
    if (!mapped)
    {
        pr_err("Can't map the memory to kernel space.\n");
        return NULL;
    }

    pr_info("base address: %pa, size: %zx, mapped: %p\n", &dev_base, dev_size, mapped);

    return mapped;
///////////////////////////////////////////////////////////////////////////////////////////////    


    // int fd;
    // void *mapped;
    
    // printf("sizeof(off_t) %d\n", sizeof(off_t));

    // if ((fd = open("/dev/mem", O_RDWR | O_SYNC)) == -1)
    // {
    //     printf("Can't open /dev/mem\n");
    //     return (void *)0;
    // }

    // mapped = mmap(0, dev_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, dev_base);
    // // close(fd);

    // printf("base address: %llx, size: %x, mapped: %p\n", dev_base, dev_size, mapped);

    // if (mapped == (void *)-1)
    // {
    //     printf("Can't map the memory to user space.\n");
    //     return (void *)0;
    // }
    // return mapped;
}


//-------------------------------------------------------------------------------------------
bool create_rp1(rp1_t **rp1, void *base)
{    
    rp1_t *r = kzalloc(sizeof(rp1_t), GFP_KERNEL);
    if (r == NULL) return false;

    r->rp1_peripherial_base = base;
    r->gpio_base = base + RP1_IO_BANK0_BASE;
    r->pads_base = base + RP1_PADS_BANK0_BASE;
    r->rio_out = (volatile uint32_t *)(base + RP1_RIO0_BASE + RIO_OUT_OFFSET);
    r->rio_output_enable = (volatile uint32_t *)(base + RP1_RIO0_BASE + RIO_OE_OFFSET);
    r->rio_nosync_in = (volatile uint32_t *)(base + RP1_RIO0_BASE + RIO_NOSYNC_IN_OFFSET);
    *rp1 = r;
    return true;
}
//------------------------------------------------------------------------------------------------
bool create_pin(uint8_t pinnumber, rp1_t *rp1)
{    
    gpio_pin_t *newpin = kzalloc(sizeof(gpio_pin_t), GFP_KERNEL);
    if(newpin == NULL) return false;

    newpin->number = pinnumber;

    // each gpio has a status and control register
    // adjacent to each other. control = status + 4 (uint8_t)
    newpin->status = (uint32_t *)(rp1->gpio_base + 8 * pinnumber);
    newpin->ctrl = (uint32_t *)(rp1->gpio_base + 8 * pinnumber + 4);
    newpin->pad = (uint32_t *)(rp1->pads_base + PADS_BANK0_GPIO_OFFSET + pinnumber * 4);

    // set the function
    *(newpin->ctrl + RP1_ATOM_CLR_OFFSET / 4) = CTRL_MASK_FUNCSEL; // first clear the bits
    *(newpin->ctrl + RP1_ATOM_SET_OFFSET / 4) = CTRL_FUNCSEL_RIO;  // now set the value we need

    rp1->pins[pinnumber] = newpin;
    return true;
}
//---------------------------------------------------------------------------------------------------------
bool create_pin_2(uint8_t pinnumber, rp1_t *rp1, uint32_t funcmask)
{
    gpio_pin_t *newpin = kzalloc(sizeof(gpio_pin_t), GFP_KERNEL);
    if(newpin == NULL) return false;

    newpin->number = pinnumber;

    // each gpio has a status and control register
    // adjacent to each other. control = status + 4 (uint8_t)
    newpin->status = (uint32_t *)(rp1->gpio_base + 8 * pinnumber);
    newpin->ctrl = (uint32_t *)(rp1->gpio_base + 8 * pinnumber + 4);
    newpin->pad = (uint32_t *)(rp1->pads_base + PADS_BANK0_GPIO_OFFSET + pinnumber * 4);

    // set the function
    *(newpin->ctrl + RP1_ATOM_CLR_OFFSET / 4) = CTRL_MASK_FUNCSEL; // first clear the bits
    *(newpin->ctrl + RP1_ATOM_SET_OFFSET / 4) = funcmask;  // now set the value we need

    rp1->pins[pinnumber] = newpin;
    //printf("pin %d stored in pins array %p\n", pinnumber, rp1->pins[pinnumber]);

    return true;
}
//------------------------------------------------------------------------------------------------------
int pin_enable_output(uint8_t pinnumber, rp1_t *rp1)
{

   // printf("Attempting to enable output\n");
   
    // first enable the pad to output
    // pads needs to have OD[7] -> 0 (don't disable output)
    // and                IE[6] -> 0 (don't enable input)
    // we use atomic access to the bit clearing alias with a mask
    // divide the offset by 4 since we're doing uint32* math

    volatile uint32_t *writeadd = rp1->pins[pinnumber]->pad + RP1_ATOM_CLR_OFFSET / 4;

    //printf("attempting write for %p at %p\n", rp1->pins[pinnumber]->pad, writeadd);

    *writeadd = PADS_MASK_OUTPUT;

    // now set the RIO output enable using the atomic set alias
    *(rp1->rio_output_enable + RP1_ATOM_SET_OFFSET / 4) = 1 << rp1->pins[pinnumber]->number;

    return 0;
}
//---------------------------------------------------------------------------
void pin_on(rp1_t *rp1, uint8_t pin)
{
    *(rp1->rio_out + RP1_ATOM_SET_OFFSET / 4) = 1 << pin;
}
//------------------------------------------------------------------------------
void pin_off(rp1_t *rp1, uint8_t pin)
{
    *(rp1->rio_out + RP1_ATOM_CLR_OFFSET / 4) = 1 << pin;
}

//-------------------------------------------------------------------------------
void setup_spi_pins(rp1_t *rp1)
{
    create_pin_2(8, rp1, 0x00);     // CS0
    create_pin_2(9, rp1, 0x00);     // MISO
    create_pin_2(10, rp1, 0x00);    // MOSI
    create_pin_2(11, rp1, 0x00);    // SCLK
}

////////////////////////////////////////////////////////////////////////////////////////////
//                        m y _ s p i _ r e a d 
///////////////////////////////////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------------------
void my_spi_read(rp1_spi_instance_t *spi, uint8_t *data)                         
{
//    if (spi->txcount != 0)  return SPI_BUSY;
   // int purgecount = 0;
    //spi_status_t res = 
    //rp1_spi_purge_rx_fifo(spi, &purgecount);
    
    // how this works
    // 1. We stuff the TX FIFO with dummy data (zeros, but can be anything you want) until it is full,
    //    or we have stuffed the number of bytes we want to read (we write in order to generate the 
    //    clock pulses to the slave, which sends us data to read)
    // 2. We then set the CS pin to active transmission
    // 3. We then continue to feed the TX FIFO with dummy data until we have sent all the bytes 
    //    we want to send, if we didn't manage them all in step 1
    // 4. As we are feeding the TX FIFO with dummy data, we are clocking out data from the slave
    //    which we read from the RX fifo and discard
    // 5. Once we have sent all the data, we then read the RX FIFO until it is empty, and store the data
    //    in the buffer we were passed
    // 6. The CS pin is turned off by the hardware when the last bit is clocked out

    // pre-stuff the TX buffer with dummy data
    // while((*(volatile uint32_t *)(spi->regbase + DW_SPI_SR) & DW_SPI_SR_TF_NOT_FULL) && (spi->txcount > 0))
    for(int i = 0; i < 16; ++i) { *(volatile uint8_t *)(spi->regbase + DW_SPI_DR) = (uint8_t)0x00; }
   
   
    // set the CS pin - since we have pre-stuffed data, the clock should start here
    // note the behaviour of te CS pin (active low, or high) is determined by the hardware
    // and the GPIO / PAD settings, but default is active low
    
    *(volatile uint32_t *)(spi->regbase + DW_SPI_SER) = 1 << 0;  
    
//    int inbyte = 0;
    // // keep loading data into the tx fifo and also see if we have anything to read in the rx fifo
    // while((*(volatile uint32_t *)(spi->regbase + DW_SPI_SR) & DW_SPI_SR_TF_NOT_FULL) && (spi->txcount > 0))
    // {
    //     *(volatile uint8_t *)(spi->regbase + DW_SPI_DR) = (uint8_t)0x00;
    //     spi->txcount--;
    //     // check if there is data to read (check status register for Read Fifo Not Empty)
    //     if(*(volatile uint32_t *)(spi->regbase + DW_SPI_SR) & DW_SPI_SR_RF_NOT_EMPT)
    //     {
    //         data[inbyte] = *(volatile uint8_t *)(spi->regbase + DW_SPI_DR);
    //         inbyte++;
    //     }
    // }

    // // read the remaining bytes from the buffer
    int inbyte = 0;
    uint32_t n;
    do  {
            n = *(volatile uint32_t *)(spi->regbase + DW_SPI_RXFLR);
            while (n--) {data[inbyte++] = *(volatile uint8_t *)(spi->regbase + DW_SPI_DR); }
        } while (inbyte < 16);

    for(int i = 0; i < 16; i += 2) // change byte
        {
        uint8_t t = data[i];
        data[i] = data[i + 1];
        data[i + 1] = t;
        }

   *(volatile uint32_t *)(spi->regbase + DW_SPI_SER) = 0;      
  //  return SPI_OK;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////
void spi_init(void)
{
     /////////////////////////////////////////////////////////
    //                   R P 1
    ////////////////////////////////////////////////////////
    // get the peripheral base address
    peripheral_base_address = mapgpio(RP1_BAR1, RP1_BAR1_LEN);
    if (peripheral_base_address == NULL) { pr_err("unable to map base\n"); return;} 

    // create a rp1 device    
    if (!create_rp1(&rp1, peripheral_base_address)) {pr_err("unable to create rp1\n"); return;}      
        
    /////////////////////////////////////////////////////////
    //                      S P I    
    /////////////////////////////////////////////////////////
    
    // create a spi instance
   // rp1_spi_instance_t *spi;
    if (!rp1_spi_create(rp1, 0, &spireg)) {pr_err("unable to create spi\n"); return; }
    
    // disable the SPI
    *(volatile uint32_t *)(spireg->regbase + DW_SPI_SSIENR) = 0x0;    
    setup_spi_pins(rp1);

    // set the speed - this is the divisor from 200MHz in the RPi5
    //*(volatile uint32_t *)(spireg->regbase + DW_SPI_BAUDR) = 20; //20;
    *(volatile uint32_t *)(spireg->regbase + DW_SPI_BAUDR) = 8; //25 Mhz;
    pr_info("\nbaudr: %d MHz\n", 200/(*(volatile uint32_t *)(spireg->regbase + DW_SPI_BAUDR)));

    // set mode - CPOL = 0, CPHA = 1 (Mode 1)    
    // read control
    uint32_t reg_ctrlr0 = *(volatile uint32_t *)(spireg->regbase + DW_SPI_CTRLR0);    
    reg_ctrlr0 |= DW_PSSI_CTRLR0_SCPHA;       
    // update the control reg
    *(volatile uint32_t *)(spireg->regbase + DW_SPI_CTRLR0) = reg_ctrlr0;
    // reg_ctrlr0 = *(volatile uint32_t *)(spi->regbase + DW_SPI_CTRLR0);
    // printf("ctrlr0 after setting (might be the same as before if mode was already set): %x\n", reg_ctrlr0);    

    *(volatile uint32_t *)(spireg->regbase + 0x18) = 0; // TXFTLR
    *(volatile uint32_t *)(spireg->regbase + 0x1C) = 0; // RXFTLR

    // clear interrupts by reading the interrupt status register
   // uint32_t reg_icr = *(volatile uint32_t *)(spireg->regbase + DW_SPI_ICR);
     *(volatile uint32_t *)(spireg->regbase + 0x48) = 0x7F; // ICR
        
    *(volatile uint32_t *)(spireg->regbase + DW_SPI_SSIENR) = 0x1;    
}

//----------------------------------------------------------------------------------
void spi_close(void)
{
   pr_info("RP1 SPI kernel module exit\n");

    if (spireg)
        kfree(spireg);
    if (rp1)
        kfree(rp1);
    if (peripheral_base_address)
        iounmap(peripheral_base_address);    
}