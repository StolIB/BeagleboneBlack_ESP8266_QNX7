/*--------------------------------Pin Configuration--------------------------------
**
** Beaglebone Black Pins           -> ESP-12F Witty Cloud
** UART Tx - Pin 13  - Connector 9 -> Rx
** UART Rx - Pin 11  - Connector 9 -> Tx
** GND     - Pin 1/2 - Connector 9 -> GND
*/

//--------------------------------Headers--------------------------------
#include <stdio.h>
#include <stdlib.h>
#include <devctl.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <share.h>
#include <string.h>
#include <stdint.h>         // For unit32 types
#include <mqueue.h>
#include <sys/iofunc.h>
#include <sys/dispatch.h>
#include <sys/neutrino.h>
#include <sys/types.h>
#include <sys/mman.h>       // For mmap_device_io()
#include <hw/inout.h>       // For in32() and out32()
#include <hw/spi-master.h>
#include <stddef.h>

//--------------------------------Global Definitions--------------------------------
// Registers required to configure and use the pins
#define AM335X_CONTROL_MODULE_BASE (uint64_t)   0x44E10000
#define AM335X_CONTROL_MODULE_SIZE (size_t)     0x00001448
#define AM335X_GPIO_SIZE                        0x00001000
#define AM335X_GPIO1_BASE                       0x4804C000  // TRM pp 182 for GPIO1
#define AM335X_GPIO2_BASE                       0x481AC000  // TRM pp 183 for GPIO2

// GPIO configuration registers
#define GPIO_OE                                 0x134       // Output Enable register
#define GPIO_DATAOUT                            0x13C       // Data Out register

// GPIO pins
#define GPIO1_02                                (1<<2)      // Pin 05 - GPIO1_06
#define GPIO1_03                                (1<<3)      // Pin 06 - GPIO1_07
#define GPIO1_06                                (1<<6)      // Pin 03 - GPIO1_06
#define GPIO1_07                                (1<<7)      // Pin 04 - GPIO1_07
#define GPIO2_06                                (1<<6)      // Pin 45 - GPIO2_06
#define GPIO2_07                                (1<<7)      // Pin 46 - GPIO2_07
#define GPIO2_08                                (1<<8)      // Pin 43 - GPIO2_08
#define GPIO2_09                                (1<<9)      // Pin 44 - GPIO2_09
#define GPIO2_10                                (1<<10)     // Pin 41 - GPIO2_10
#define GPIO2_11                                (1<<11)     // Pin 42 - GPIO2_11

// GPIO/SPI/UART Pin pinmux mode overview
#define GPIO1_2_pinConfig                       0x808       // conf_gpmc_ad2 (TRM pp 1456) for GPIO1_2
#define GPIO1_3_pinConfig                       0x80C       // conf_gpmc_ad3 (TRM pp 1456) for GPIO1_3
#define GPIO1_6_pinConfig                       0x818       // conf_gpmc_ad6 (TRM pp 1456) for GPIO1_6
#define GPIO1_7_pinConfig                       0x81C       // conf_gpmc_ad7 (TRM pp 1456) for GPIO1_7
#define GPIO2_6_pinConfig                       0x840       // conf_gpmc_a0 (TRM pp 1456) for GPIO2_6
#define GPIO2_7_pinConfig                       0x844       // conf_gpmc_a1 (TRM pp 1456) for GPIO2_7
#define GPIO2_8_pinConfig                       0x848       // conf_gpmc_a2 (TRM pp 1456) for GPIO2_8
#define GPIO2_9_pinConfig                       0x84C       // conf_gpmc_a3 (TRM pp 1456) for GPIO2_9
#define GPIO2_10_pinConfig                      0x850       // conf_gpmc_a4 (TRM pp 1456) for GPIO2_10
#define GPIO2_11_pinConfig                      0x854       // conf_gpmc_a5 (TRM pp 1456) for GPIO2_11
#define P9_12_pinConfig                         0x878       // conf_gpmc_ben1 (TRM pp 1364) for GPIO1_28,  P9_12
#define uart1_ctsn_pinConfig                    0x978       // conf_uart1_ctsn (TRM pp 1458) for uart1_ctsn
#define uart1_rtsn_pinConfig                    0x97C       // conf_uart1_rtsn (TRM pp 1458) for uart1_rtsn
#define uart1_rxd_pinConfig                     0x980       // conf_uart1_rxd (TRM pp 1458) for uart1_rxd
#define uart1_txd_pinConfig                     0x984       // conf_uart1_txd (TRM pp 1458) for uart1_txd
#define spi_cs_1_pinConfig                      0x99C       // conf_conf_mcasp0_ahclkr (TRM pp 1458) for spi_1_cs
#define spi_d0_1_pinConfig                      0x994       // conf_conf_mcasp0_fsx (TRM pp 1458) for spi_1_d0
#define spi_d1_1_pinConfig                      0x998       // conf_conf_mcasp0_axr0 (TRM pp 1458) for spi_1_d1
#define spi_sclk_1_pinConfig                    0x990       // conf_conf_mcasp0_aclkx (TRM pp 1458) for spi_1_sclk

// UART - Path & Message size
#define UART_PATH                               "/dev/ser2"

// (No longer used) Configuring the pinmux for UART1 - PIN MUX Configuration strut values (TRM pp 1446)
#define PU_ENABLE                               0x00
#define PU_DISABLE                              0x01
#define PU_PULL_UP                              0x01
#define PU_PULL_DOWN                            0x00
#define RECV_ENABLE                             0x01
#define RECV_DISABLE                            0x00
#define SLEW_FAST                               0x00
#define SLEW_SLOW                               0x01

// GPMC_Configurations for PIN MUX
#define PIN_MODE_0                              0x00
#define PIN_MODE_1                              0x01
#define PIN_MODE_2                              0x02
#define PIN_MODE_3                              0x03
#define PIN_MODE_4                              0x04
#define PIN_MODE_5                              0x05
#define PIN_MODE_6                              0x06
#define PIN_MODE_7                              0x07

// SPI - Path & Message size
#define SPI_PATH                                "/dev/spi1"
#define TSPI_WRITE_7                            (7)
#define TSPI_WRITE_SHORT                        (8)
#define TSPI_WRITE_12                           (12)
#define TSPI_WRITE_16                           (16)
#define TSPI_WRITE_32                           (32)

// Message Queues
#define MESSAGESIZE                             1000

// Resource manager
#define INTNUM                                  0

//--------------------------------Global Variables--------------------------------
// UART - Message size
char char_read_temp_buffer                      [32];
char char_read_buffer                           [32];
char char_write_buffer                          [32];

//Struct for configuring the Pin Multiplexer
typedef union _CONF_MODULE_PIN_STRUCT {             // See TRM Page 1446
    unsigned int d32;
    struct {                                        // Name: field size unsigned
        int conf_mmode :                        3;  // LSB
        unsigned int conf_puden :               1;
        unsigned int conf_putypesel :           1;
        unsigned int conf_rxactive :            1;
        unsigned int conf_slewctrl :            1;
        unsigned int conf_res_1 :               13; // Reserved
        unsigned int conf_res_2 :               12; // Reserved MSB
    } b;
}   _CONF_MODULE_PIN;

// File openers and return variable
int file;
int ret;

// Data packets declarations for SPI communications
uint8_t write_buffer                            [256 * 1024];
uint8_t read_buffer                             [256 * 1024];
uint8_t reg1[8]     =                           {0xff, 0x3E, 0x11, 0x00, 0x44, 0x22, 0x66, 0x00};                       // Data to be sent for testing purposes
uint8_t reg2[7]     =                           {0xfb, 0x04, 0x04, 0x3b, 0x40, 0x40, 0x3f};                             // Data to be sent for testing purposes
uint8_t reg3[7]     =                           {0x0f, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02};                             // Data to be sent for testing purposes
uint8_t reg4[8]     =                           {0x48, 0x65, 0x72, 0x65, 0x21, 0x21, 0x21, 0x00};                       // Data to be sent for testing purposes
uint8_t reg5[12]    =                           {0x48, 0x65, 0x6c, 0x6c, 0x6f, 0x20, 0x53, 0x6c, 0x61, 0x76, 0x65};
uint8_t reg6[16]    =                           {0x41, 0x72, 0x65, 0x20, 0x79, 0x6f, 0x75, 0x20, 0x61, 0x6c, 0x69, 0x76, 0x65, 0x3f, 0x0};

// Resource manager
static resmgr_connect_funcs_t   connect_funcs;
static resmgr_io_funcs_t        io_funcs;
static iofunc_attr_t            attr;

//--------------------------------Prototypes--------------------------------
void * interrupt_thread (void * data);
void Pin_status();
void Pin_control(unsigned int pin, unsigned int value);
void Pin_config(int mode, unsigned int puden, unsigned int putypesel, unsigned int rxactive, unsigned int slewctrl, unsigned int pin);
int spiopen();
int spisetcfg();
int spigetdevinfo();
int spiwrite(int iterations);
int spiclose();
int UART_write();
int UART_read();


//-------------------------------------------------------------------------
int main(int argc, char **argv) {
    thread_pool_attr_t    pool_attr;
    resmgr_attr_t         resmgr_attr;
    dispatch_t            *dpp;
    thread_pool_t         *tpp;
    int                   id;


    if((dpp = dispatch_create()) == NULL) {
        fprintf(stderr, "%s: Unable to allocate dispatch handle.\n", argv[0]);
        return EXIT_FAILURE;
    }

    memset(&pool_attr, 0, sizeof pool_attr);
    pool_attr.handle = dpp; 
    pool_attr.context_alloc = dispatch_context_alloc; 
    pool_attr.block_func = dispatch_block;  
    pool_attr.unblock_func = dispatch_unblock; 
    pool_attr.handler_func = dispatch_handler; 
    pool_attr.context_free = dispatch_context_free;
    pool_attr.lo_water = 2;
    pool_attr.hi_water = 4;
    pool_attr.increment = 1;
    pool_attr.maximum = 50;

    if((tpp = thread_pool_create(&pool_attr, POOL_FLAG_EXIT_SELF)) == NULL) {
        fprintf(stderr, "%s: Unable to initialize thread pool.\n", argv[0]);
        return EXIT_FAILURE;
    }

    iofunc_func_init(_RESMGR_CONNECT_NFUNCS, &connect_funcs, _RESMGR_IO_NFUNCS, &io_funcs);
    iofunc_attr_init(&attr, S_IFNAM | 0666, 0, 0);
        
    memset(&resmgr_attr, 0, sizeof resmgr_attr);
    resmgr_attr.nparts_max = 1;
    resmgr_attr.msg_max_size = 2048;

    if((id = resmgr_attach(dpp, &resmgr_attr, "/dev/ESP", _FTYPE_ANY, 0, &connect_funcs, &io_funcs, &attr)) == -1) {
        fprintf(stderr, "%s: Unable to attach name.\n", argv[0]);
        return EXIT_FAILURE;
    }

    /* Start the thread that will handle interrupt events. */
    pthread_create (NULL, NULL, interrupt_thread, NULL);

    /* Never returns */
    thread_pool_start(tpp);
}
//--------------------------------Function Definitions--------------------------------
void * interrupt_thread (void * data) {
    struct sigevent event;
    int             id;

    /* fill in "event" structure */
    memset(&event, 0, sizeof(event));
    event.sigev_notify = SIGEV_INTR;

    /* Obtain I/O privileges */
    ThreadCtl( _NTO_TCTL_IO, 0 );

    /* intNum is the desired interrupt level */
    id = InterruptAttachEvent (INTNUM, &event, 0);

    printf("Welcome to the QNX Momentics mqueue receive process\n");

    mqd_t   qd;
    char    buf[MESSAGESIZE];

    struct  mq_attr attr;

    // Configuring the location for the mqueue
    const char * MqueueLocation = "/test_queue";                // Will be located /dev/mqueue/test_queue

    int count = 0;
    qd = mq_open(MqueueLocation, O_RDONLY);                     // MqueueLocation should be opened on the node where the queue was established
    while (1) {
        if (qd != -1) {
            mq_getattr(qd, &attr);
            // Stating mqueue configuration from the sending process
            printf ("max. %ld msgs, %ld bytes; waiting: %ld\n", attr.mq_maxmsg, attr.mq_msgsize, attr.mq_curmsgs);

            // Dequeue strings from the sending process
            while (mq_receive(qd, buf, MESSAGESIZE, NULL) > 0) {
                printf("dequeue: '%s'", buf);                 // Print out the messages to this terminal
                strcpy(char_write_buffer, buf);
                //--------UART Code--------
                ret = 0;
                file = open(UART_PATH, O_RDWR);
                UART_write();
                // printf("Count value: %d\n", count++);
                if (close(file) == -1) {
                    printf("close failed: %s\n", strerror(errno));
                }
            }
            mq_close(qd);
        }
    }

    while (1) {
        InterruptWait (NULL, NULL);
        /*  do something about the interrupt,
         *  perhaps updating some shared
         *  structures in the resource manager 
         *
         *  unmask the interrupt when done
         */
        InterruptUnmask(INTNUM, id);
    }
}

// Checking the status of the pins
void Pin_status() {
    printf("0. val = %#8x\n\n",AM335X_GPIO1_BASE);

    //--------Setting GPIO_1 Pins for use with GPIO07 and GPIO06--------
    // GPIO global pointers
    uintptr_t gpio1_base = NULL;
    uintptr_t control_module = NULL;

    volatile uint32_t val = 0;

    control_module = mmap_device_io(AM335X_CONTROL_MODULE_SIZE, AM335X_CONTROL_MODULE_BASE);    //Reading the state of the registers

    // Displaying the current PIN MUX configurations
    if (control_module) {
        in32s(&val,1,control_module+GPIO1_2_pinConfig);
        printf("Current pinmux configuration for      GPIO1_02  = %#010x\n", val);

        in32s(&val,1,control_module+GPIO1_3_pinConfig);
        printf("Current pinmux configuration for      GPIO1_03  = %#010x\n", val);

        in32s(&val,1,control_module+GPIO1_6_pinConfig);
        printf("Current pinmux configuration for      GPIO1_06  = %#010x\n", val);

        in32s(&val,1,control_module+GPIO1_7_pinConfig);
        printf("Current pinmux configuration for      GPIO1_07  = %#010x\n", val);

        in32s(&val,1,control_module+P9_12_pinConfig);
        printf("Current pinmux configuration for      GPIO1_28  = %#010x\n", val);

        in32s(&val,1,control_module+GPIO2_6_pinConfig);
        printf("Current pinmux configuration for      GPIO2_06  = %#010x\n", val);

        in32s(&val,1,control_module+GPIO2_7_pinConfig);
        printf("Current pinmux configuration for      GPIO2_07  = %#010x\n", val);

        in32s(&val,1,control_module+GPIO2_8_pinConfig);
        printf("Current pinmux configuration for      GPIO2_08  = %#010x\n", val);

        in32s(&val,1,control_module+GPIO2_9_pinConfig);
        printf("Current pinmux configuration for      GPIO2_09  = %#010x\n", val);

        in32s(&val,1,control_module+GPIO2_10_pinConfig);
        printf("Current pinmux configuration for      GPIO2_10  = %#010x\n", val);

        in32s(&val,1,control_module+GPIO2_11_pinConfig);
        printf("Current pinmux configuration for      GPIO2_11  = %#010x\n", val);

        in32s(&val,1,control_module+spi_cs_1_pinConfig);
        printf("Current pinmux configuration for   SPI 1 CS  = %#010x\n", val);

        in32s(&val,1,control_module+spi_d0_1_pinConfig);
        printf("Current pinmux configuration for   SPI 1 D0  = %#010x\n", val);

        in32s(&val,1,control_module+spi_d1_1_pinConfig);
        printf("Current pinmux configuration for   SPI 1 D1  = %#010x\n", val);

        in32s(&val,1,control_module+spi_sclk_1_pinConfig);
        printf("Current pinmux configuration for SPI 1 SCLK  = %#010x\n", val);

        in32s(&val,1,control_module+uart1_ctsn_pinConfig);
        printf("Current pinmux configuration for UART 1 CTSN = %#010x\n", val);

        in32s(&val,1,control_module+uart1_rtsn_pinConfig);
        printf("Current pinmux configuration for UART 1 RTSN = %#010x\n", val);

        in32s(&val,1,control_module+uart1_rxd_pinConfig);
        printf("Current pinmux configuration for UART 1 RXD  = %#010x\n", val);

        in32s(&val,1,control_module+uart1_txd_pinConfig);
        printf("Current pinmux configuration for UART 1 TXD  = %#010x\n\n", val);

        munmap_device_io(control_module, AM335X_CONTROL_MODULE_SIZE);
    }
}

// GPIO Pin control
void Pin_control(unsigned int pin, unsigned int value) {
    unsigned int new_pin = (pin & value);

    printf("PIN: %d\n",pin);
    printf("VALUE: %d\n",value);
    printf("New PIN VALUE: %d\n\n",new_pin);

    // uintptr_t gpio1_base    = NULL;
    uintptr_t gpio2_base    = NULL;
    volatile uint32_t val   = 0;

    // gpio1_base = mmap_device_io(AM335X_GPIO_SIZE , AM335X_GPIO1_BASE);  //Reading the state of the registers
    gpio2_base = mmap_device_io(AM335X_GPIO_SIZE , AM335X_GPIO2_BASE);  //Reading the state of the registers

    // Write value to output enable
    if (gpio2_base) {
        val &= ~(pin);
        out32(gpio2_base + GPIO_OE, val);

        val = in32(gpio2_base + GPIO_DATAOUT);      // Read in current value
        printf("1. val = %#8x\n", val);             // Debug
        
        if (new_pin) {                              // Determining whether the PIN is required to set LOW or HIGH
            val |= (pin);                           // Set the pattern to display (set next value, i++)
            printf("3. val = %#8x\n\n", val);       // Debug
        } if (!new_pin) {
            val &= ~(pin);                          // Clear the bits that we might change
            printf("2. val = %#8x\n", val);         // Debug            
        }
        out32(gpio2_base + GPIO_DATAOUT, val);      // Write new value
        delay(250);

        munmap_device_io(gpio2_base, AM335X_GPIO_SIZE);
    }
}

// Pin configuration through configuring the PIN MUX settings
void Pin_config(int mode, unsigned int puden, unsigned int putypesel, unsigned int rxactive, unsigned int slewctrl, unsigned int pin) {
    int MODE                = mode;
    unsigned int PUDEN      = puden;
    unsigned int PUTYPESEL  = putypesel;
    unsigned int RXACTIVE   = rxactive;
    unsigned int SLEWCTRL   = slewctrl;
    unsigned int PIN        = pin;

    // Test code to configure the PinMux for use with UART1
    uintptr_t control_module = mmap_device_io(AM335X_CONTROL_MODULE_SIZE, AM335X_CONTROL_MODULE_BASE);
    uintptr_t gpio1_base = mmap_device_io(AM335X_GPIO_SIZE , AM335X_GPIO1_BASE);

    // Set up pin mux for the pins we are going to use (see page 1354 of TRM)
    volatile _CONF_MODULE_PIN pinConfigGPMC;

    // Pin configuration strut
    pinConfigGPMC.d32 = 0;

    // Pin MUX register default setup for input (GPIO input, disable pull up/down - Mode 7)
    pinConfigGPMC.b.conf_slewctrl   = SLEWCTRL;     // Select between faster or slower slew rate
    pinConfigGPMC.b.conf_rxactive   = RXACTIVE;     // Input enable value for the PAD
    pinConfigGPMC.b.conf_putypesel  = PUTYPESEL;    // Pad pullup/pulldown type selection
    pinConfigGPMC.b.conf_puden      = PUDEN;        // Pad pullup/pulldown enable
    pinConfigGPMC.b.conf_mmode      = MODE;         // Pad functional signal mux select 0 - 7

    // Write to the PinMux registers for UART1
    out32(control_module + PIN, pinConfigGPMC.d32);
}

// Opening the communication link to SPI1
int spiopen() {
    //  Open SPI1
    if((file = spi_open(SPI_PATH) ) < 0) {  // Open SPI1
        printf("Error while opening Device File!!\n\n");
    } else {
        printf("SPI1 Opened Successfully\n\n");
    }
    return ret;

}

// Configuring the SPI1
int spisetcfg() {
    spi_cfg_t spicfg;

    // Setting the correct SPI operation mode
    // CLK POL -> 0 || CLK PHA -> 1 || For later use maybe?
    spicfg.mode         = (8 & SPI_MODE_CHAR_LEN_MASK)|SPI_MODE_CKPHASE_HALF|SPI_MODE_CKPOL_HIGH;   //SPI Mode
    spicfg.clock_rate   = 100000;                                                                   //Clock rate
     // spicfg.clock_rate  = 1000000;

    // Configuring the SPI bus
    ret = spi_setcfg(file,0,&spicfg);
    if (ret != EOK){
        fprintf(stdout,"spi_setcfg failed: %d\n\n", ret);
    } else {
        fprintf(stdout,"spi_setcfg successful: %d\n\n", ret);
    }
}

// Checking device info
int spigetdevinfo() {
    spi_devinfo_t devinfo;
    spi_cfg_t spicfg;

    // Retreiving the information on the SPI bus
    ret = spi_getdevinfo(file,SPI_DEV_ID_NONE,&devinfo);
    if (ret != EOK){
        fprintf(stdout,"spi_getdevinfo failed: %d\n", ret);
    } else {
        fprintf(stdout,"spi_getdevinfo successful: %d\n", ret);
        fprintf(stdout,"Device ID: %d\n",devinfo.device);
        fprintf(stdout,"Device Name: %s\n",devinfo.name);
        fprintf(stdout,"Device Mode: %d\n",spicfg.mode);
        fprintf(stdout,"Device Speed: %d\n\n",spicfg.clock_rate);
    }
}

// SPI write function
int spiwrite(int iterations) {
    int loop_state      = 1;
    int counter         = 0;
    char output[128]    = "";
    char values[50]     = "";

    while(loop_state) {
        printf("Counter = %d\n",counter);

        // --------Write to SPI1--------
        // Writing data to the packet
        for (int i = 0; i < sizeof(reg4); i++) {
            write_buffer[i] = reg4[i];
        }
        ret = spi_write(file,SPI_DEV_LOCK,write_buffer,TSPI_WRITE_SHORT);   // Writing to the buffer
        if (ret == -1){
            printf("spi_write failed: %s\n", strerror(errno));
        } else {
            fprintf(stdout,"spi_write successful! \n");
            fprintf(stdout,"Number of bytes: %d\n",ret);

            // Checking what was sent to the buffer
            for (int i = 0; i < ret; i++) {
                //  printf("Loop %d\n",i);
                sprintf(values, "%#0x ", write_buffer[i]);
                strcat(output, values);
                memset(values, 0, sizeof(values));
            }
            fprintf(stdout, "Sent - Data: %s\n\n", output);
            memset(output, 0, sizeof(output));
        }

        // --------Read from SPI1--------(Placed here for testing purposes)
        // Reading from the buffer
        ret = spi_read(file,SPI_DEV_LOCK,read_buffer,TSPI_WRITE_32);
        if(ret == -1){
            printf("spi_read failed: %s\n", strerror(errno));
        } else {
            fprintf(stdout,"spi_read successful! \n");
            fprintf(stdout,"Number of bytes: %d\n",ret);

            // Displaying what was read from the buffer
            for(int i = 0; i < ret; i++) {
                //  printf("Loop %d\n",i);
                sprintf(values, "%#0x ", read_buffer[i]);
                strcat(output, values);
                memset(values, 0, sizeof(values));
            }
            fprintf(stdout, "Read - Data: %s\n\n", output);
            memset(output, 0, sizeof(output));
        }

        sleep(1);
        counter++;
        printf("Count = %d\n\n",counter);

        if(iterations == counter){
            loop_state = 0;
        }
    }
}

// Closing the communication link to SPI1
int spiclose() {
    ret = spi_close(file);
    fprintf(stdout,"Value returned from spi_close: %d\n\n",ret);
}

// UART write function
int UART_write() {
    ret = 0;
    printf("\nTx: %s", char_write_buffer);
    ret = write(file, &char_write_buffer, strlen(char_write_buffer));
    printf("\nTx: Number of Bytes: %d\n\n", ret);
    return ret;
}

// UART read function
int UART_read() {
    ret = 0;
    ret = read(file, &char_read_temp_buffer, sizeof(char_read_temp_buffer));
    // char_read_buffer[ret] = '\0'    //Use this if you want to include either "\n" or the "\r" at the end
    char_read_temp_buffer[ret-1] = '\0';
    while (!strcmp(char_read_temp_buffer, "")) {
        ret = read(file, &char_read_temp_buffer, sizeof(char_read_temp_buffer));
        char_read_temp_buffer[ret-1] = '\0';
    }
    strcpy(char_read_buffer, char_read_temp_buffer);
    printf("\nRx: Number of Bytes: %d", ret);
    printf("\nRx: %s\n", char_read_buffer);
    return ret;
}
