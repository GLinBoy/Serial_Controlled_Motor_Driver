/******************************************************************************
devRegisters.c
Serial controlled motor driver firmware
marshall.taylor@sparkfun.com
7-8-2016
https://github.com/sparkfun/Serial_Controlled_Motor_Driver/

See github readme for mor information.

This code is released under the [MIT License](http://opensource.org/licenses/MIT).
Please review the LICENSE.md file included with this example. If you have any questions 
or concerns with licensing, please contact techsupport@sparkfun.com.
Distributed as-is; no warranty is given.
******************************************************************************/
#include <project.h>
#include <stdint.h>
#include <stdbool.h>
#include "devRegisters.h"
#include "SCMD_config.h"
#include "registerHandlers.h"

//Set accessable table size here:
#define REGISTER_TABLE_LENGTH 128

//bits for access table
#define UNSERVICED 0x01
#define GLOBAL_READ_ONLY 0x02
#define USER_READ_ONLY 0x04

static uint8_t registerTable[REGISTER_TABLE_LENGTH];
static uint8_t registerAccessTable[REGISTER_TABLE_LENGTH]; // b0: unserviced, b1: global read-only, b2: user read-only

#define BB_INV_2_9             0x00000001
#define BB_INV_10_17           0x00000002
#define BB_INV_18_25           0x00000004
#define BB_INV_26_33           0x00000008
#define BB_BRIDGE_SLV_L        0x00000010
#define BB_BRIDGE_SLV_H        0x00000020
#define BB_DRIVER_ENABLE       0x00000040
#define BB_FORCE_UPDATE        0x00000080
#define BB_MASTER_LOCK         0x00000100
#define BB_USER_LOCK           0x00000200
#define BB_FSAFE_TIME          0x00000400
#define BB_REM_WRITE           0x00000800
#define BB_REM_READ            0x00001000

volatile uint32_t busyBitMemory = 0;

void initDevRegisters( void )
{
    //Explicitly set all values in the table
    int i = 0;
    for( i = 0; i < REGISTER_TABLE_LENGTH; i++ )
    {
        registerTable[i] = 0x00;
        registerAccessTable[i] = 0x00;
    }
    
    //Define register lockable states:
	// Global read-only (unlock with master key)
    registerAccessTable[SCMD_FID] = GLOBAL_READ_ONLY;
    registerAccessTable[SCMD_ID] = GLOBAL_READ_ONLY;
    registerAccessTable[SCMD_SLAVE_ADDR] = GLOBAL_READ_ONLY;
    registerAccessTable[SCMD_CONFIG_BITS] = GLOBAL_READ_ONLY;
    registerAccessTable[SCMD_SLV_POLL_CNT] = GLOBAL_READ_ONLY;
    registerAccessTable[SCMD_SLV_TOP_ADDR] = GLOBAL_READ_ONLY;
    registerAccessTable[SCMD_REM_DATA_RD] = GLOBAL_READ_ONLY;
    registerAccessTable[SCMD_U_PORT_CLKDIV_U] = GLOBAL_READ_ONLY;
    registerAccessTable[SCMD_U_PORT_CLKDIV_L] = GLOBAL_READ_ONLY;
    registerAccessTable[SCMD_U_PORT_CLKDIV_CTRL] = GLOBAL_READ_ONLY;
    registerAccessTable[SCMD_E_PORT_CLKDIV_U] = GLOBAL_READ_ONLY;
    registerAccessTable[SCMD_E_PORT_CLKDIV_L] = GLOBAL_READ_ONLY;
    registerAccessTable[SCMD_E_PORT_CLKDIV_CTRL] = GLOBAL_READ_ONLY;
    registerAccessTable[SCMD_U_BUS_UART_BAUD] = GLOBAL_READ_ONLY;
    registerAccessTable[SCMD_GEN_TEST_WORD] = GLOBAL_READ_ONLY;
    registerAccessTable[SCMD_STATUS_1] = GLOBAL_READ_ONLY;
	
	// User lockable (starts unlocked)
    registerAccessTable[SCMD_LOCAL_MASTER_LOCK] = USER_READ_ONLY;
    registerAccessTable[SCMD_MOTOR_A_INVERT] = USER_READ_ONLY;
    registerAccessTable[SCMD_MOTOR_B_INVERT] = USER_READ_ONLY;
    registerAccessTable[SCMD_BRIDGE] = USER_READ_ONLY;
    registerAccessTable[SCMD_INV_2_9] = USER_READ_ONLY;
    registerAccessTable[SCMD_INV_10_17] = USER_READ_ONLY;
    registerAccessTable[SCMD_INV_18_25] = USER_READ_ONLY;
    registerAccessTable[SCMD_INV_26_33] = USER_READ_ONLY;
    registerAccessTable[SCMD_BRIDGE_SLV_L] = USER_READ_ONLY;
    registerAccessTable[SCMD_BRIDGE_SLV_H] = USER_READ_ONLY;
    registerAccessTable[SCMD_FSAFE_TIME] = USER_READ_ONLY;
    registerAccessTable[SCMD_DRIVER_ENABLE] = USER_READ_ONLY;
    registerAccessTable[SCMD_UPDATE_RATE] = USER_READ_ONLY;
    registerAccessTable[SCMD_MASTER_LOCK] = USER_READ_ONLY;
    registerAccessTable[SCMD_E_BUS_SPEED] = USER_READ_ONLY;
    registerAccessTable[SCMD_CONTROL_1] = USER_READ_ONLY;
    registerAccessTable[SCMD_FSAFE_CTRL] = USER_READ_ONLY;
    
    setColdInitValues();
}

void setColdInitValues( void )
{
    //Set hardcoded initial values --
    //ID_WORD, START_SLAVE_ADDR, MAX_SLAVE_ADDR are defined within the included files
    // (writing straight to the table forgos 'isChanged' check)
    registerTable[SCMD_LOCAL_MASTER_LOCK] = MASTER_LOCK_KEY;  //Start unlocked
    registerTable[SCMD_LOCAL_USER_LOCK] = USER_LOCK_KEY;  //Start unlocked
    registerTable[SCMD_MASTER_LOCK] = MASTER_LOCK_KEY;  //Start unlocked
    registerTable[SCMD_USER_LOCK] = USER_LOCK_KEY;  //Start unlocked
    
	writeDevRegister(SCMD_FID, FIRMWARE_VERSION);
    writeDevRegister(SCMD_ID, ID_WORD);
    writeDevRegister(SCMD_CONFIG_BITS, CONFIG_BITS_REG_Read() ^ 0x0F);    // Read HW config bits
    writeDevRegister(SCMD_FSAFE_TIME, 0 );
    writeDevRegister(SCMD_REM_OFFSET, 0x01);
    writeDevRegister(SCMD_REM_ADDR, POLL_ADDRESS);
    writeDevRegister(SCMD_SLV_POLL_CNT, 0);
    writeDevRegister(SCMD_SLAVE_ADDR, 0x10); // No one should ever ask for data on 0x10
    writeDevRegister(SCMD_UPDATE_RATE, 0x0A);
    //Set default baud based on jumpers
    if( readDevRegister( SCMD_CONFIG_BITS ) == 0x0D ) registerTable[SCMD_U_BUS_UART_BAUD] = 0x06; // Set 57600
    else if( readDevRegister( SCMD_CONFIG_BITS ) == 0x0E ) registerTable[SCMD_U_BUS_UART_BAUD] = 0x07; // Set 115200
    else registerTable[SCMD_U_BUS_UART_BAUD] = 0x02; // Default baud rate of 9600
    registerTable[SCMD_E_BUS_SPEED] = 0x01;
    registerTable[SCMD_FSAFE_CTRL] = (SCMD_FSAFE_CYCLE_EXP | SCMD_FSAFE_CYCLE_USER | SCMD_FSAFE_DRIVE_KILL); //Mode reinit both ports, stop motors
    writeDevRegister(SCMD_MST_E_IN_FN, SCMD_M_IN_CYCLE_USER | SCMD_M_IN_CYCLE_EXP);
    
    setWarmInitValues();
}

void setWarmInitValues( void )
{
    writeDevRegister(SCMD_MA_DRIVE, 0x80);
    writeDevRegister(SCMD_MB_DRIVE, 0x80);
    writeDevRegister(SCMD_S1A_DRIVE, 0x80);
    writeDevRegister(SCMD_S1B_DRIVE, 0x80);
    writeDevRegister(SCMD_S2A_DRIVE, 0x80);
    writeDevRegister(SCMD_S2B_DRIVE, 0x80);
    writeDevRegister(SCMD_S3A_DRIVE, 0x80);
    writeDevRegister(SCMD_S3B_DRIVE, 0x80);
    writeDevRegister(SCMD_S4A_DRIVE, 0x80);
    writeDevRegister(SCMD_S4B_DRIVE, 0x80);
    writeDevRegister(SCMD_S5A_DRIVE, 0x80);
    writeDevRegister(SCMD_S5B_DRIVE, 0x80);
    writeDevRegister(SCMD_S6A_DRIVE, 0x80);
    writeDevRegister(SCMD_S6B_DRIVE, 0x80);
    writeDevRegister(SCMD_S7A_DRIVE, 0x80);
    writeDevRegister(SCMD_S7B_DRIVE, 0x80);
    writeDevRegister(SCMD_S8A_DRIVE, 0x80);
    writeDevRegister(SCMD_S8B_DRIVE, 0x80);
    writeDevRegister(SCMD_S9A_DRIVE, 0x80);
    writeDevRegister(SCMD_S9B_DRIVE, 0x80);
    writeDevRegister(SCMD_S10A_DRIVE, 0x80);
    writeDevRegister(SCMD_S10B_DRIVE, 0x80);
    writeDevRegister(SCMD_S11A_DRIVE, 0x80);
    writeDevRegister(SCMD_S11B_DRIVE, 0x80);
    writeDevRegister(SCMD_S12A_DRIVE, 0x80);
    writeDevRegister(SCMD_S12B_DRIVE, 0x80);
    writeDevRegister(SCMD_S13A_DRIVE, 0x80);
    writeDevRegister(SCMD_S13B_DRIVE, 0x80);
    writeDevRegister(SCMD_S14A_DRIVE, 0x80);
    writeDevRegister(SCMD_S14B_DRIVE, 0x80);
    writeDevRegister(SCMD_S15A_DRIVE, 0x80);
    writeDevRegister(SCMD_S15B_DRIVE, 0x80);
    writeDevRegister(SCMD_S16A_DRIVE, 0x80);
    writeDevRegister(SCMD_S16B_DRIVE, 0x80);    
}

uint8_t readDevRegister( uint8_t regNumberIn )
{
    if( regNumberIn >= REGISTER_TABLE_LENGTH )
    {
        incrementDevRegister( SCMD_REG_OOR_CNT );
        return 0;
    }
    else
    {
        return registerTable[regNumberIn];
    }
}

void writeDevRegister( uint8_t regNumberIn, uint8_t dataToWrite )
{
    if( regNumberIn >= REGISTER_TABLE_LENGTH )
    {
        incrementDevRegister( SCMD_REG_OOR_CNT );
    }
    else
    {
        if(registerAccessTable[regNumberIn] & USER_READ_ONLY)
        {
            //Register is user lockable
            if((registerTable[SCMD_LOCAL_USER_LOCK] == USER_LOCK_KEY)||(registerTable[SCMD_LOCAL_MASTER_LOCK] == MASTER_LOCK_KEY)) //check if either key is in
            {
                //Unlocked
                registerTable[regNumberIn] = dataToWrite;
                //*** TEMP CODE ***//
                setBusyBitMem( regNumberIn );
                if(busyBitMemory)
                {
                    setStatusBit(SCMD_BUSY_BIT);
                }
                //*** TEMP CODE ***//
                registerAccessTable[regNumberIn] |= UNSERVICED; //set bit
            }
            else
            {
                //Locked -- no access
                incrementDevRegister( SCMD_REG_RO_WRITE_CNT );
            }
        }
        else if(registerAccessTable[regNumberIn] & GLOBAL_READ_ONLY)
        {
            //Register is user lockable
            if(registerTable[SCMD_LOCAL_MASTER_LOCK] == MASTER_LOCK_KEY)
            {
                //Unlocked
                registerTable[regNumberIn] = dataToWrite;
                //*** TEMP CODE ***//
                setBusyBitMem( regNumberIn );
                if(busyBitMemory)
                {
                    setStatusBit(SCMD_BUSY_BIT);
                }
                //*** TEMP CODE ***//
                registerAccessTable[regNumberIn] |= UNSERVICED; //set bit
            }
            else
            {
                //Locked -- no access
                incrementDevRegister( SCMD_REG_RO_WRITE_CNT );
            }
        }
        else
        {
            //Fully unlocked register
            registerTable[regNumberIn] = dataToWrite;
            registerAccessTable[regNumberIn] |= UNSERVICED; //set bit
            //*** TEMP CODE ***//
            setBusyBitMem( regNumberIn );
            if(busyBitMemory)
            {
                setStatusBit(SCMD_BUSY_BIT);
            }
            //*** TEMP CODE ***//
        }
    }
}

void writeDevRegisterUnprotected( uint8_t regNumberIn, uint8_t dataToWrite )
{
    if( regNumberIn >= REGISTER_TABLE_LENGTH )
    {
        incrementDevRegister( SCMD_REG_OOR_CNT );
    }
    else
    {
        registerTable[regNumberIn] = dataToWrite;
    }
}

void incrementDevRegister( uint8_t regNumberIn )
{
    if( regNumberIn >= REGISTER_TABLE_LENGTH )
    {
        incrementDevRegister( SCMD_REG_OOR_CNT );
    }
    else
    {
        if(registerAccessTable[regNumberIn] & USER_READ_ONLY)
        {
            //Register is user lockable
            if((registerTable[SCMD_LOCAL_USER_LOCK] == USER_LOCK_KEY)||(registerTable[SCMD_LOCAL_MASTER_LOCK] == MASTER_LOCK_KEY)) //check if either key is in
            {
                //Unlocked
                registerTable[regNumberIn]++;
                registerAccessTable[regNumberIn] |= UNSERVICED; //set bit
            }
            else
            {
                //Locked -- no access
                incrementDevRegister( SCMD_REG_RO_WRITE_CNT );
            }
        }
        else if(registerAccessTable[regNumberIn] & GLOBAL_READ_ONLY)
        {
            //Register is user lockable
            if(registerTable[SCMD_LOCAL_MASTER_LOCK] == MASTER_LOCK_KEY)
            {
                //Unlocked
                registerTable[regNumberIn]++;
                registerAccessTable[regNumberIn] |= UNSERVICED; //set bit
            }
            else
            {
                //Locked -- no access
                incrementDevRegister( SCMD_REG_RO_WRITE_CNT );
            }
        }
        else
        {
            //Fully unlocked register
            registerTable[regNumberIn]++;
            registerAccessTable[regNumberIn] |= UNSERVICED; //set bit
        }
    }
}

bool getChangedStatus( uint8_t regNumberIn )
{
    if( regNumberIn >= REGISTER_TABLE_LENGTH )
    {
        incrementDevRegister( SCMD_REG_OOR_CNT );
        return false;
    }
    else
    {
        return registerAccessTable[regNumberIn] & UNSERVICED; //set bit
    }
}

void clearChangedStatus( uint8_t regNumberIn )
{
    if( regNumberIn >= REGISTER_TABLE_LENGTH )
    {
        incrementDevRegister( SCMD_REG_OOR_CNT );
    }
    else
    {
        registerAccessTable[regNumberIn] &= ~UNSERVICED; //clear bit
    }
}

void setBusyBitMem( uint8_t offset )// Send register value
{
    switch(offset)
    {
        case SCMD_INV_2_9:
        busyBitMemory |= BB_INV_2_9;
        break;
        case SCMD_INV_10_17:
        busyBitMemory |= BB_INV_10_17;
        break;
        case SCMD_INV_18_25:
        busyBitMemory |= BB_INV_18_25;
        break;
        case SCMD_INV_26_33:
        busyBitMemory |= BB_INV_26_33;
        break;
        case SCMD_BRIDGE_SLV_L:
        busyBitMemory |= BB_BRIDGE_SLV_L;
        break;
        case SCMD_BRIDGE_SLV_H:
        busyBitMemory |= BB_BRIDGE_SLV_H;
        break;
        case SCMD_DRIVER_ENABLE:
        busyBitMemory |= BB_DRIVER_ENABLE;
        break;
        case SCMD_FORCE_UPDATE:
        busyBitMemory |= BB_FORCE_UPDATE;
        break;
        case SCMD_MASTER_LOCK:
        busyBitMemory |= BB_MASTER_LOCK;
        break;
        case SCMD_USER_LOCK:
        busyBitMemory |= BB_USER_LOCK;
        break;
        case SCMD_FSAFE_TIME:
        busyBitMemory |= BB_FSAFE_TIME;
        break;
        case SCMD_REM_WRITE:
        busyBitMemory |= BB_REM_WRITE;
        break;
        case SCMD_REM_READ:
        busyBitMemory |= BB_REM_READ;
        default:
        break;
    }
}

void clearBusyBitMem( uint8_t offset )// Send register value
{
    switch(offset)
    {
        case SCMD_INV_2_9:
        busyBitMemory &= ~( BB_INV_2_9 );
        break;
        case SCMD_INV_10_17:
        busyBitMemory &= ~( BB_INV_10_17 );
        break;
        case SCMD_INV_18_25:
        busyBitMemory &= ~( BB_INV_18_25 );
        break;
        case SCMD_INV_26_33:
        busyBitMemory &= ~( BB_INV_26_33 );
        break;
        case SCMD_BRIDGE_SLV_L:
        busyBitMemory &= ~( BB_BRIDGE_SLV_L );
        break;
        case SCMD_BRIDGE_SLV_H:
        busyBitMemory &= ~( BB_BRIDGE_SLV_H );
        break;
        case SCMD_DRIVER_ENABLE:
        busyBitMemory &= ~( BB_DRIVER_ENABLE );
        break;
        case SCMD_FORCE_UPDATE:
        busyBitMemory &= ~( BB_FORCE_UPDATE );
        break;
        case SCMD_MASTER_LOCK:
        busyBitMemory &= ~( BB_MASTER_LOCK );
        break;
        case SCMD_USER_LOCK:
        busyBitMemory &= ~( BB_USER_LOCK );
        break;
        case SCMD_FSAFE_TIME:
        busyBitMemory &= ~( BB_FSAFE_TIME );
        break;
        case SCMD_REM_WRITE:
        busyBitMemory &= ~( BB_REM_WRITE );
        break;
        case SCMD_REM_READ:
        busyBitMemory &= ~( BB_REM_READ );
        break;
        default:
        break;
    }
}