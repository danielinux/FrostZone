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
#define TIOCSWINSZ	0x5414

/****************************************/
/****************************************/
/* Crypto devices: /dev/hash, /dev/aes, /dev/pka */

/* /dev/hash ioctls */
#define IOCTL_HASH_SET_ALGO     0x00    /* arg = HASH_ALGO_* */

/* Hash algorithm IDs */
#define HASH_ALGO_SHA256        0
#define HASH_ALGO_SHA224        1

/* /dev/aes ioctls */
#define IOCTL_AES_SET_MODE      0x00    /* arg = struct aes_mode_req * */
#define IOCTL_AES_SET_KEY       0x01    /* arg = struct aes_key_req * */
#define IOCTL_AES_SET_IV        0x02    /* arg = uint8_t iv[16] */

/* AES direction */
#define AES_DIR_ENCRYPT         0
#define AES_DIR_DECRYPT         1

/* AES chaining modes */
#define AES_MODE_ECB            0
#define AES_MODE_CBC            1
#define AES_MODE_CTR            2
#define AES_MODE_GCM            3

struct aes_mode_req {
    uint32_t direction;     /* AES_DIR_* */
    uint32_t mode;          /* AES_MODE_* */
};

struct aes_key_req {
    uint32_t size;          /* key size in bytes: 16 or 32 */
    uint8_t  key[32];
};

/* /dev/pka ioctls */
#define IOCTL_PKA_ECC_MUL      0x00    /* arg = struct pka_ecc_mul_req * */
#define IOCTL_PKA_ECDSA_SIGN   0x01    /* arg = struct pka_ecdsa_sign_req * */
#define IOCTL_PKA_ECDSA_VERIFY 0x02    /* arg = struct pka_ecdsa_verify_req * */

/* ECC curve parameter sizes (P-256) */
#define PKA_MAX_KEY_SIZE        66      /* up to P-521 */

struct pka_ecc_mul_req {
    uint32_t modulus_size;          /* bytes (e.g. 32 for P-256) */
    uint32_t scalar_size;           /* bytes */
    uint32_t coef_sign;             /* 0 or 1 */
    uint8_t  coef_a[PKA_MAX_KEY_SIZE];
    uint8_t  coef_b[PKA_MAX_KEY_SIZE];
    uint8_t  modulus[PKA_MAX_KEY_SIZE];
    uint8_t  point_x[PKA_MAX_KEY_SIZE];
    uint8_t  point_y[PKA_MAX_KEY_SIZE];
    uint8_t  scalar[PKA_MAX_KEY_SIZE];
    uint8_t  prime_order[PKA_MAX_KEY_SIZE];
    /* output (filled by kernel) */
    uint8_t  result_x[PKA_MAX_KEY_SIZE];
    uint8_t  result_y[PKA_MAX_KEY_SIZE];
};

struct pka_ecdsa_sign_req {
    uint32_t order_size;            /* bytes */
    uint32_t modulus_size;          /* bytes */
    uint32_t coef_sign;
    uint8_t  coef_a[PKA_MAX_KEY_SIZE];
    uint8_t  coef_b[PKA_MAX_KEY_SIZE];
    uint8_t  modulus[PKA_MAX_KEY_SIZE];
    uint8_t  base_x[PKA_MAX_KEY_SIZE];
    uint8_t  base_y[PKA_MAX_KEY_SIZE];
    uint8_t  prime_order[PKA_MAX_KEY_SIZE];
    uint8_t  hash[PKA_MAX_KEY_SIZE];
    uint8_t  random_k[PKA_MAX_KEY_SIZE];
    uint8_t  private_key[PKA_MAX_KEY_SIZE];
    /* output (filled by kernel) */
    uint8_t  sig_r[PKA_MAX_KEY_SIZE];
    uint8_t  sig_s[PKA_MAX_KEY_SIZE];
    uint8_t  error;
};

struct pka_ecdsa_verify_req {
    uint32_t order_size;
    uint32_t modulus_size;
    uint32_t coef_sign;
    uint8_t  coef_a[PKA_MAX_KEY_SIZE];
    uint8_t  modulus[PKA_MAX_KEY_SIZE];
    uint8_t  base_x[PKA_MAX_KEY_SIZE];
    uint8_t  base_y[PKA_MAX_KEY_SIZE];
    uint8_t  prime_order[PKA_MAX_KEY_SIZE];
    uint8_t  pub_x[PKA_MAX_KEY_SIZE];
    uint8_t  pub_y[PKA_MAX_KEY_SIZE];
    uint8_t  sig_r[PKA_MAX_KEY_SIZE];
    uint8_t  sig_s[PKA_MAX_KEY_SIZE];
    uint8_t  hash[PKA_MAX_KEY_SIZE];
    /* output (filled by kernel) */
    uint32_t valid;                 /* 1 = valid, 0 = invalid */
};


/* KEYBOARD */
#define     KDGKBMODE   0x4B44  /* gets current keyboard mode */
#define     KDSKBMODE   0x4B45  /* sets current keyboard mode */
#define     K_RAW       0x00
#define     K_XLATE     0x01
#define     K_MEDIUMRAW 0x02
#define     K_UNICODE   0x03
#define     K_OFF       0x04


#endif

