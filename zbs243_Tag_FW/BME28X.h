#pragma once

#include "soc/zbs243/timer.h"
#include "soc/zbs243/i2c.h"

#define BME280_32BIT_ENABLE
#include "bme280_defs.h"

/// @brief Device address already shifted left for write mode
__xdata const uint8_t ADDRESS = BME280_I2C_ADDR_PRIM << 1;

/// @brief Bus communication function pointer which should be mapped to the platform specific read functions of the user
///
/// @param[in]     reg_addr : 8bit register address of the sensor
/// @param[out]    reg_data : Data from the specified address
/// @param[in]     len      : Length of the reg_data array
/// @retval 0 for Success
/// @retval Non-zero for Failure
int8_t read(const uint8_t reg_addr, __xdata uint8_t *reg_data, uint8_t len)
{
    reg_data[0] = reg_addr; // reuse out buffer. assumes it is at least a byte big. Do not try to read ZERO bytes using this func...

    __xdata struct I2cTransaction transactions[] = {
        {
            .deviceAddr = ADDRESS, // write mode
            .numBytes = 1,
            .bytes = reg_data,
        },
        {
            .deviceAddr = ADDRESS + 1,
            .numBytes = len,
            .bytes = reg_data,
        },
    };

    // pr("trx 0x%02X 0x%02X %d\n", transactions[0].deviceAddr, transactions[1].deviceAddr, len);
    return i2cTransact(transactions, sizeof(transactions) / sizeof(*transactions));
}

__xdata uint8_t i2cbuffer[18];

/// @brief Bus communication function pointer which should be mapped to
/// the platform specific write functions of the user
///
/// @param[in]     reg_addr : 8bit register address of the sensor
/// @param[out]    reg_data : Data to the specified address
/// @param[in]     len      : Length of the reg_data array
/// @retval 0 for Success
/// @retval Non-zero for Failure
int8_t write(const uint8_t reg_addr, __xdata const uint8_t *reg_data, uint8_t len)
{
    __xdata struct I2cTransaction transaction = {
        .deviceAddr = ADDRESS,
        .numBytes = (len + 1),
        .bytes = i2cbuffer,
    };

    i2cbuffer[0] = reg_addr;
    memcpy(i2cbuffer + 1, reg_data, len);

    return i2cTransact(&transaction, 1);
}

inline bool readRegister(__xdata const uint8_t address, __xdata uint8_t *data)
{
    return read(address, data, 1) == 0;
}

inline void readCalibData(__xdata struct bme280_calib_data *calibData)
{
    __xdata uint8_t buffer[BME280_LEN_TEMP_PRESS_CALIB_DATA] = {0};

    if (read(BME280_REG_TEMP_PRESS_CALIB_DATA, buffer, BME280_LEN_TEMP_PRESS_CALIB_DATA) != 0)
    {
        // pr("CNRCD1\n");
        return;
    }

    calibData->dig_t1 = BME280_CONCAT_BYTES(buffer[1], buffer[0]);
    calibData->dig_t2 = (int16_t)BME280_CONCAT_BYTES(buffer[3], buffer[2]);
    calibData->dig_t3 = (int16_t)BME280_CONCAT_BYTES(buffer[5], buffer[4]);
    calibData->dig_p1 = BME280_CONCAT_BYTES(buffer[7], buffer[6]);
    calibData->dig_p2 = (int16_t)BME280_CONCAT_BYTES(buffer[9], buffer[8]);
    calibData->dig_p3 = (int16_t)BME280_CONCAT_BYTES(buffer[11], buffer[10]);
    calibData->dig_p4 = (int16_t)BME280_CONCAT_BYTES(buffer[13], buffer[12]);
    calibData->dig_p5 = (int16_t)BME280_CONCAT_BYTES(buffer[15], buffer[14]);
    calibData->dig_p6 = (int16_t)BME280_CONCAT_BYTES(buffer[17], buffer[16]);
    calibData->dig_p7 = (int16_t)BME280_CONCAT_BYTES(buffer[19], buffer[18]);
    calibData->dig_p8 = (int16_t)BME280_CONCAT_BYTES(buffer[21], buffer[20]);
    calibData->dig_p9 = (int16_t)BME280_CONCAT_BYTES(buffer[23], buffer[22]);
    calibData->dig_h1 = buffer[25];

    // pr("Calib data:\n");
    // pr("t1: %d\n", calibData->dig_t1);
    // pr("t2: %d\n", calibData->dig_t2);
    // pr("t3: %d\n", calibData->dig_t3);
    // pr("p1: %d\n", calibData->dig_p1);
    // pr("p2: %d\n", calibData->dig_p2);
    // pr("p3: %d\n", calibData->dig_p3);
    // pr("p4: %d\n", calibData->dig_p4);
    // pr("p5: %d\n", calibData->dig_p5);
    // pr("p6: %d\n", calibData->dig_p6);
    // pr("p7: %d\n", calibData->dig_p7);
    // pr("p8: %d\n", calibData->dig_p8);
    // pr("p9: %d\n", calibData->dig_p9);
    // pr("h1: %d\n", calibData->dig_h1);

    if (read(BME280_REG_HUMIDITY_CALIB_DATA, buffer, BME280_LEN_HUMIDITY_CALIB_DATA) != 0)
    {
        // pr("CNRCD2\n");
        return;
    }

    calibData->dig_h2 = (int16_t)BME280_CONCAT_BYTES(buffer[1], buffer[0]);
    calibData->dig_h3 = buffer[2];
    calibData->dig_h4 = (int16_t)(int8_t)buffer[3] * 16 | (int16_t)(buffer[4] & 0x0F);
    calibData->dig_h5 = (int16_t)(int8_t)buffer[5] * 16 | (int16_t)(buffer[4] >> 4);
    calibData->dig_h6 = (int8_t)buffer[6];
}

inline void setOsrsHumidity(__xdata struct bme280_settings *settings)
{
    __xdata uint8_t data = settings->osr_h & BME280_CTRL_HUM_MSK;
    write(BME280_REG_CTRL_HUM, &data, 1);

    // Humidity related changes will be only effective after a write operation to ctrl_meas register
    // Since we set pressure and temp as well we can omit this
    // if (!readRegister(BME280_REG_CTRL_MEAS, &data))
    // {
    //     // pr("CNSOH\n");
    //     return;
    // }
    // write(BME280_REG_CTRL_HUM, &data, 1);
}

inline void setOsrsPressureTemperature(__xdata struct bme280_settings *settings)
{
    __xdata uint8_t data;
    if (!readRegister(BME280_REG_CTRL_MEAS, &data))
    {
        // pr("CNSOPT\n");
        return;
    }

    data = BME280_SET_BITS(data, BME280_CTRL_PRESS, settings->osr_p);
    data = BME280_SET_BITS(data, BME280_CTRL_TEMP, settings->osr_t);

    write(BME280_REG_CTRL_MEAS, &data, 1);
}

// inline void getSettings(__xdata struct bme280_settings *settings)
// {
//     __xdata uint8_t buffer[4] = {0};
//     if (read(BME280_REG_CTRL_HUM, buffer, 4) != 0)
//     {
//         // pr("CNRS\n");
//         return;
//     }

//     settings->osr_h = BME280_GET_BITS_POS_0(buffer[0], BME280_CTRL_HUM);
//     settings->osr_p = BME280_GET_BITS(buffer[2], BME280_CTRL_PRESS);
//     settings->osr_t = BME280_GET_BITS(buffer[2], BME280_CTRL_TEMP);
//     settings->filter = BME280_GET_BITS(buffer[3], BME280_FILTER);
//     settings->standby_time = BME280_GET_BITS(buffer[3], BME280_STANDBY);
// }

inline void setMode(const uint8_t mode)
{
    __xdata uint8_t data;
    if (!readRegister(BME280_REG_PWR_CTRL, &data))
    {
        // pr("CNSM\n");
        return;
    }
    data = BME280_SET_BITS_POS_0(data, BME280_SENSOR_MODE, mode);
    write(BME280_REG_PWR_CTRL, &data, 1);
}

inline bool isMeasuring()
{
    __xdata uint8_t data;
    if (!readRegister(BME280_REG_STATUS, &data))
    {
        // pr("NMEAS\n");
        return false;
    }
    return data & BME280_STATUS_MEAS_DONE;
}

/// @brief This API is used to parse the pressure, temperature and
///  humidity data and store it in the bme280_uncomp_data structure instance.
inline void parse_sensor_data(__xdata const uint8_t *reg_data, __xdata struct bme280_uncomp_data *uncomp_data)
{
    uncomp_data->pressure = (uint32_t)reg_data[0] << BME280_12_BIT_SHIFT | mathPrvMul32x8((uint32_t)reg_data[1], 16) | mathPrvDiv32x8((uint32_t)reg_data[2], 16);
    uncomp_data->temperature = (uint32_t)reg_data[3] << BME280_12_BIT_SHIFT | mathPrvMul32x8((uint32_t)reg_data[4], 16) | mathPrvDiv32x8((uint32_t)reg_data[5], 16);
    uncomp_data->humidity = (uint32_t)reg_data[6] << BME280_8_BIT_SHIFT | (uint32_t)reg_data[7];
}

inline void getMeasurements(__xdata struct bme280_uncomp_data *data)
{
    __xdata uint8_t buffer[BME280_LEN_P_T_H_DATA] = {0};

    if (read(BME280_REG_DATA, buffer, BME280_LEN_P_T_H_DATA) != 0)
    {
        // pr("CNGM\n");
        return;
    }
    parse_sensor_data(buffer, data);
}

#define TEMPERATURE_MIN -4000
#define TEMPERATURE_MAX 8500

#define PRESSURE_MIN 30000
#define PRESSURE_MAX 110000

#define HUMIDITY_MAX 102400

/// @brief This internal API is used to compensate the raw temperature data and
/// return the compensated temperature data in integer data type.
inline int32_t compensateTemperature(const struct bme280_uncomp_data *uncomp_data, struct bme280_calib_data *calib_data)
{
    int32_t var1 = (int32_t)((uncomp_data->temperature / 8) - ((int32_t)calib_data->dig_t1 * 2));
    var1 = (var1 * ((int32_t)calib_data->dig_t2)) / 2048;
    int32_t var2 = (int32_t)((uncomp_data->temperature / 16) - ((int32_t)calib_data->dig_t1));
    var2 = (((var2 * var2) / 4096) * ((int32_t)calib_data->dig_t3)) / 16384;
    calib_data->t_fine = var1 + var2;
    int32_t temperature = (calib_data->t_fine * 5 + 128) / 256;

    if (temperature < TEMPERATURE_MIN)
    {
        temperature = TEMPERATURE_MIN;
    }
    else if (temperature > TEMPERATURE_MAX)
    {
        temperature = TEMPERATURE_MAX;
    }

    return temperature;
}

/// @brief This internal API is used to compensate the raw pressure data and
/// return the compensated pressure data in integer data type.
inline uint32_t compensatePressure(const struct bme280_uncomp_data *uncomp_data, const struct bme280_calib_data *calib_data)
{
    int32_t var1 = (((int32_t)calib_data->t_fine) / 2) - (int32_t)64000;
    int32_t var2 = (((var1 / 4) * (var1 / 4)) / 2048) * ((int32_t)calib_data->dig_p6);
    var2 = var2 + ((var1 * ((int32_t)calib_data->dig_p5)) * 2);
    var2 = (var2 / 4) + (((int32_t)calib_data->dig_p4) * 65536);
    const int32_t var3 = (calib_data->dig_p3 * (((var1 / 4) * (var1 / 4)) / 8192)) / 8;
    const int32_t var4 = (((int32_t)calib_data->dig_p2) * var1) / 2;
    var1 = (var3 + var4) / 262144;
    var1 = (((32768 + var1)) * ((int32_t)calib_data->dig_p1)) / 32768;

    // Avoid exception caused by division by zero
    if (var1)
    {
        const uint32_t var5 = (uint32_t)((uint32_t)1048576) - uncomp_data->pressure;
        uint32_t pressure = ((uint32_t)(var5 - (uint32_t)(var2 / 4096))) * 3125;

        if (pressure < 0x80000000)
        {
            pressure = (pressure << 1) / ((uint32_t)var1);
        }
        else
        {
            pressure = (pressure / (uint32_t)var1) * 2;
        }

        var1 = (((int32_t)calib_data->dig_p9) * ((int32_t)(((pressure / 8) * (pressure / 8)) / 8192))) / 4096;
        var2 = (((int32_t)(pressure / 4)) * ((int32_t)calib_data->dig_p8)) / 8192;
        pressure = (uint32_t)((int32_t)pressure + ((var1 + var2 + calib_data->dig_p7) / 16));

        if (pressure < PRESSURE_MIN)
        {
            pressure = PRESSURE_MIN;
        }
        else if (pressure > PRESSURE_MAX)
        {
            pressure = PRESSURE_MAX;
        }
        return pressure;
    }

    return PRESSURE_MIN;
}

/// @brief This internal API is used to compensate the raw humidity data and
/// return the compensated humidity data in integer data type.
inline uint32_t compensateHumidity(const struct bme280_uncomp_data *uncomp_data, const struct bme280_calib_data *calib_data)
{
    const int32_t var1 = calib_data->t_fine - ((int32_t)76800);
    int32_t var2 = (int32_t)(uncomp_data->humidity * 16384);
    int32_t var3 = (int32_t)(((int32_t)calib_data->dig_h4) * 1048576);
    int32_t var4 = ((int32_t)calib_data->dig_h5) * var1;
    int32_t var5 = (((var2 - var3) - var4) + (int32_t)16384) / 32768;
    var2 = (var1 * ((int32_t)calib_data->dig_h6)) / 1024;
    var3 = (var1 * ((int32_t)calib_data->dig_h3)) / 2048;
    var4 = ((var2 * (var3 + (int32_t)32768)) / 1024) + (int32_t)2097152;
    var2 = ((var4 * ((int32_t)calib_data->dig_h2)) + 8192) / 16384;
    var3 = var5 * var2;
    var4 = ((var3 / 32768) * (var3 / 32768)) / 128;
    var5 = var3 - ((var4 * ((int32_t)calib_data->dig_h1)) / 16);
    var5 = (var5 < 0 ? 0 : var5);
    var5 = (var5 > 419430400 ? 419430400 : var5);
    uint32_t humidity = (uint32_t)(var5 / 4096);

    if (humidity > HUMIDITY_MAX)
    {
        humidity = HUMIDITY_MAX;
    }

    return humidity;
}

inline void compensateData(__xdata struct bme280_calib_data *calibData, __xdata struct bme280_uncomp_data *uncomp, __xdata struct bme280_data *data)
{
    data->temperature = compensateTemperature(uncomp, calibData);
    data->pressure = compensatePressure(uncomp, calibData);
    data->humidity = compensateHumidity(uncomp, calibData);
}

// inline void softReset()
// {
//     __xdata uint8_t data = BME280_SOFT_RESET_COMMAND;
//     write(BME280_REG_RESET, &data, 1);

//     __xdata bool ret;
//     uint8_t try_run = 5;
//     do
//     {
//         timerDelay(BME280_STARTUP_DELAY);
//         ret = readRegister(BME280_REG_STATUS, &data);
//     } while (ret && (try_run--) && data & BME280_STATUS_IM_UPDATE);

//     if (data & BME280_STATUS_IM_UPDATE)
//     {
//         pr("NVM cp f");
//     }
// }

inline void readSensor(__xdata struct bme280_data *data)
{
    // read chip id
    __xdata uint8_t chipId;
    if (!readRegister(BME280_REG_CHIP_ID, &chipId))
    {
        return;
    }

    if (chipId != BME280_CHIP_ID)
    {
        return;
    }
    pr("Found BME280\n");

    // softReset();

    __xdata struct bme280_calib_data calibData;
    readCalibData(&calibData);

    __xdata struct bme280_settings settings;
    // getSettings(&settings);
    settings.osr_p = BME280_OVERSAMPLING_2X;
    settings.osr_t = BME280_OVERSAMPLING_2X;
    settings.osr_h = BME280_OVERSAMPLING_2X;
    // settings.filter = 0;
    // settings.standby_time = 0;
    setOsrsHumidity(&settings);
    setOsrsPressureTemperature(&settings);

    setMode(BME280_POWERMODE_FORCED);

    while (isMeasuring())
    {
        pr("meas\n");
        timerDelay(TIMER_TICKS_PER_MS * 10);
    }

    __xdata struct bme280_uncomp_data uncomp;
    getMeasurements(&uncomp);
    compensateData(&calibData, &uncomp, data);

    // pr("BME t%lu h%lu p%lu\n", uncomp.temperature, uncomp.humidity, uncomp.pressure);
    pr("BME t%ld h%lu p%lu\n", data->temperature, data->humidity / 1024, data->pressure / 100);
}