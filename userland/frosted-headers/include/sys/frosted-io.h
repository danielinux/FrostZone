#ifndef INC_FROSTED_IO
#define INC_FROSTED_IO

/* Lowpower/reboot flag */
#define SYS_FROSTED_FADEOFF 0xFADE0FFF

/* Reboot cmds */
#define RB_STANDBY 0
#define RB_SUSPEND 4
#define RB_REBOOT  6

/****************************************/
/****************************************/
/* GPIO */
#define IOCTL_GPIO_DISABLE 0
#define IOCTL_GPIO_ENABLE 1
#define IOCTL_GPIO_SET_OUTPUT 2
#define IOCTL_GPIO_SET_INPUT 3
#define IOCTL_GPIO_SET_PULLUPDOWN 4
#define IOCTL_GPIO_SET_ALT_FUNC 5
#define IOCTL_GPIO_SET_TRIGGER  6
/* GPIO mx */
#define IOCTL_GPIOMX_DESTROY    0
#define IOCTL_GPIOMX_CREATE     1

struct gpio_req {
    uint32_t base;
    uint32_t pin;
    char *name;
};

/* IOCTL_GPIO_SET_PULLUPDOWN Arguments: */
#define IOCTL_GPIO_PUPD_NONE       ((uint32_t)0)
#define IOCTL_GPIO_PUPD_PULLUP     ((uint32_t)1)
#define IOCTL_GPIO_PUPD_PULLDOWN   ((uint32_t)2)

/* IOCTL_GPIO_SET_TRIGGER Arguments: */
#define GPIO_TRIGGER_NONE   ((uint32_t)0)
#define GPIO_TRIGGER_FALL   ((uint32_t)1)
#define GPIO_TRIGGER_RAISE  ((uint32_t)2)
#define GPIO_TRIGGER_TOGGLE ((uint32_t)3)

/****************************************/
/****************************************/
/* I2C */
#define IOCTL_I2C_SET_MASTER    0
#define IOCTL_I2C_SET_SLAVE     1
#define IOCTL_I2C_SET_ADDR7     2
#define IOCTL_I2C_SET_ADDR10    3
#define IOCTL_I2C_SET_ADDR7_2   4
#define IOCTL_I2C_SET_SPEED     5

/****************************************/
/****************************************/
/* NETWORK */
/* Routing table calls. */
#define SIOCADDRT	0x890B		/* add routing table entry	*/
#define SIOCDELRT	0x890C		/* delete routing table entry	*/
#define SIOCRTMSG	0x890D		/* call to routing system	*/

/* Socket configuration controls. */
#define SIOCGIFNAME	0x8910		/* get iface name		*/
#define SIOCSIFLINK	0x8911		/* set iface channel		*/
#define SIOCGIFCONF	0x8912		/* get iface list		*/
#define SIOCGIFFLAGS	0x8913		/* get flags			*/
#define SIOCSIFFLAGS	0x8914		/* set flags			*/
#define SIOCGIFADDR	0x8915		/* get PA address		*/
#define SIOCSIFADDR	0x8916		/* set PA address		*/
#define SIOCGIFDSTADDR	0x8917		/* get remote PA address	*/
#define SIOCSIFDSTADDR	0x8918		/* set remote PA address	*/
#define SIOCGIFBRDADDR	0x8919		/* get broadcast PA address	*/
#define SIOCSIFBRDADDR	0x891a		/* set broadcast PA address	*/
#define SIOCGIFNETMASK	0x891b		/* get network PA mask		*/
#define SIOCSIFNETMASK	0x891c		/* set network PA mask		*/
#define SIOCGIFMETRIC	0x891d		/* get metric			*/
#define SIOCSIFMETRIC	0x891e		/* set metric			*/
#define SIOCGIFMEM	0x891f		/* get memory address (BSD)	*/
#define SIOCSIFMEM	0x8920		/* set memory address (BSD)	*/
#define SIOCGIFMTU	0x8921		/* get MTU size			*/
#define SIOCSIFMTU	0x8922		/* set MTU size			*/
#define SIOCSIFNAME	0x8923		/* set interface name */
#define	SIOCSIFHWADDR	0x8924		/* set hardware address 	*/
#define SIOCGIFENCAP	0x8925		/* get/set encapsulations       */
#define SIOCSIFENCAP	0x8926
#define SIOCGIFHWADDR	0x8927		/* Get hardware address		*/
#define SIOCGIFSLAVE	0x8929		/* Driver slaving support	*/
#define SIOCSIFSLAVE	0x8930
#define SIOCADDMULTI	0x8931		/* Multicast address lists	*/
#define SIOCDELMULTI	0x8932
#define SIOCGIFINDEX	0x8933		/* name -> if_index mapping	*/
#define SIOGIFINDEX	SIOCGIFINDEX	/* misprint compatibility :-)	*/
#define SIOCSIFPFLAGS	0x8934		/* set/get extended flags set	*/
#define SIOCGIFPFLAGS	0x8935
#define SIOCDIFADDR	0x8936		/* delete PA address		*/
#define	SIOCSIFHWBROADCAST	0x8937	/* set hardware broadcast addr	*/
#define SIOCGIFCOUNT	0x8938		/* get number of devices */

#define SIOCETHTOOL 0x8946      /* Ethtool interface        */

/****************************************/
/****************************************/
/* L3GD20 */
#define IOCTL_L3GD20_WRITE_CTRL_REG     0
#define IOCTL_L3GD20_READ_CTRL_REG      1

/****************************************/
/****************************************/
/* LSM303DLHC */
#define IOCTL_LSM303DLHC_WRITE_CTRL_REG     0
#define IOCTL_LSM303DLHC_READ_CTRL_REG      1

/****************************************/
/****************************************/
/* STMPE811 */
#define IOCTL_STMPE811_WRITE_CTRL_REG     0
#define IOCTL_STMPE811_READ_CTRL_REG      1

/****************************************/
/****************************************/
/* Framebuffer */
#define IOCTL_FB_GET_VSCREENINFO     0x00
#define IOCTL_FB_GET_FSCREENINFO     IOCTL_FB_GET_VSCREENINFO
#define IOCTL_FB_GETCMAP             0x04
#define IOCTL_FB_PUTCMAP             0x05

/* Possible fields for fb_*_screninfo type */

#define FB_TYPE_PIXELMAP 0x00
#define FB_TYPE_TEXT     0x01

enum fb_pixel_format {
    FB_PF_ARGB8888,
    FB_PF_RGB888,
    FB_PF_RGB565,
    FB_PF_ARGB15555,
    FB_PF_ARGB4444,
    FB_PF_CMAP256
};

struct fb_var_screeninfo {
    uint8_t type;
    uint8_t *smem_start;
    uint32_t smem_len;
    uint32_t xres; /* visible resolution */
    uint32_t yres;
    uint32_t xoffset; /* offset from virtual to visible */
    uint32_t yoffset; /* resolution */

    uint32_t bits_per_pixel;
    enum fb_pixel_format pixel_format;
};

#define fb_fix_screeninfo fb_var_screeninfo

/* Terminal TIOCGWINSZ */
struct winsize
{
  unsigned short ws_row;	/* rows, in characters */
  unsigned short ws_col;	/* columns, in characters */
  unsigned short ws_xpixel;	/* horizontal size, pixels */
  unsigned short ws_ypixel;	/* vertical size, pixels */
};
#define TIOCGWINSZ	0x5413

/* KEYBOARD */
#define     KDGKBMODE   0x4B44  /* gets current keyboard mode */
#define     KDSKBMODE   0x4B45  /* sets current keyboard mode */
#define     K_RAW       0x00
#define     K_XLATE     0x01
#define     K_MEDIUMRAW 0x02
#define     K_UNICODE   0x03
#define     K_OFF       0x04


#endif

