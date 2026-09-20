//
// Created by Hoppz on 25-12-9.
//
#include "libzds/drone/light_program.h"

#include <cassert>
#include <iostream>
#include <utility>

namespace libzds{

uint8_t light_program_next_byte(int& index, const char* data)
{
    // if ( index >= strlen(data)) {
    //     assert(false);
    // }

    return static_cast<uint8_t>(data[index++]);
}

uint8_t light_program_parse_command(int& index, const char* data)
{
    return light_program_next_byte(index,data);
}

uint32_t light_program_parse_var_uint(int& index, const char* data)
{
    uint32_t value = 0;
    int shift = 0;
    uint8_t byte = 255;

    while (byte >= 128) {
        byte = light_program_next_byte(index,data);
        value |= (byte & 0x7f) << shift;
        shift += 7;
    }

    return value;
}

Color light_program_parse_color(int& index, const char* data)
{
    Color color;
    color.r = light_program_next_byte(index,data);
    color.g = light_program_next_byte(index,data);
    color.b = light_program_next_byte(index,data);
    return color;
}

uint32_t light_program_parse_duration(int& index, const char* data)
{
    return light_program_parse_var_uint(index,data) * 20;
}

uint32_t light_program_parse_timestamp(int& index, const char* data)
{
    return light_program_parse_var_uint(index,data);
}

Status LightProgram::init(const char *u8data, size_t strSize)
{
	if ( _init ) {
		return Status::NotSupported("already init");
	}

    int index = 0;
    uint32_t te_timestamp = 0;
    uint32_t te_endTime = 0;
    uint32_t te_duration = 0;
    Color te_startColor;
    Color te_endColor;
    bool te_isFade;
    uint8_t iterations = 0;                         ///< loop 的次数
    std::vector<std::pair<int,int>> loopStack;  ///< <index, iterations>

    auto advanceTimeBy = [&](uint32_t duration_) {
        te_timestamp = te_endTime;
        te_duration = duration_;
        te_endTime = te_timestamp + duration_;

        if (te_isFade) {
            te_startColor = te_endColor;
            te_isFade = false;
        }
    };

    auto setToConstantColor = [&](Color color_, uint32_t duration_) {
        advanceTimeBy(duration_);
        te_startColor = color_;
        //TODO maybe need?
        //TODO te_endColor = color_;
    };

    auto setToConstantGray = [&](uint8_t grayLevel_, uint32_t duration_) {
        advanceTimeBy(duration_);
        te_startColor = Color(grayLevel_, grayLevel_, grayLevel_);
        //TODO maybe need?
        //TODO te_endColor = color_;
    };

    auto fadeToColor = [&](Color color_, uint32_t duration) {
        advanceTimeBy(duration);

        te_endColor = color_;
        te_isFade = true;
    };

    auto fadeToGray = [&](uint8_t grayLevel_, uint32_t duration) {
        advanceTimeBy(duration);
        te_endColor = Color(grayLevel_, grayLevel_, grayLevel_);
        te_isFade = true;
    };

    while (index < strSize) {
		uint8_t cmd = light_program_parse_command(index, u8data);
		uint32_t duration = 0;

		if (cmd == 0) {
			//qDebug() << QString("[cmd:%1]").arg(cmd);
			break;
		}

		switch (cmd) {
		case 1: { /* nop */
			//qDebug() << QString("[cmd:%1 nop]").arg(cmd);
			break;
		}
		case 2: { /* sleep: 休眠 duration 时间 */

			duration = light_program_parse_duration(index, u8data);
			if (duration > 0) {
				advanceTimeBy(duration);
			}
			//qDebug() << QString("[cmd:%1 sleep]").arg(cmd) << duration;
			break;
		}
		case 3: { /* wait until newTimeStamp */
			uint32_t te_data = static_cast<uint32_t>(light_program_parse_timestamp(index, u8data));
			uint32_t newTimeStamp = std::max(te_endTime, te_data);
			duration = newTimeStamp - te_endTime;
			if (duration > 0) {
				advanceTimeBy(duration);
				advanceTimeBy(0);
			}
			//qDebug() << QString("[cmd:%1 wait]").arg(cmd) << te_data;
			break;
		}
		case 4: { /* set color */
			Color color_ = light_program_parse_color(index, u8data);
			duration = light_program_parse_duration(index, u8data);
			setToConstantColor(color_, duration);
			//qDebug() << QString("[cmd:%1 set color]").arg(cmd) << color_ << duration;
			break;
		}
		case 5: { /* set gray */
			uint8_t grayLevel = light_program_next_byte(index, u8data);
			duration = light_program_parse_duration(index, u8data);
			setToConstantGray(grayLevel, duration);
			//qDebug() << QString("[cmd:%1 set gray]").arg(cmd) << grayLevel << duration;
			break;
		}
		case 6: { /* set black */
			duration = light_program_parse_duration(index, u8data);
			setToConstantGray(0, duration);
			//qDebug() << QString("[cmd:%1 set black]").arg(cmd) << duration;
			break;
		}
		case 7: { /* set white */
			duration = light_program_parse_duration(index, u8data);
			setToConstantGray(255, duration);
			//qDebug() << QString("[cmd:%1 set white]").arg(cmd) << duration;
			break;
		}
		case 8: { /* fade to color */
			Color color = light_program_parse_color(index, u8data);
			duration = light_program_parse_duration(index, u8data);
			fadeToColor(color, duration);
			//qDebug() << QString("[cmd:%1 fade to color]").arg(cmd) << color << duration;
			break;
		}
		case 9: { /* fade to gray */
			uint8_t grayLevel = light_program_next_byte(index, u8data);
			duration = light_program_parse_duration(index, u8data);
			fadeToGray(grayLevel, duration);
			//qDebug() << QString("[cmd:%1 fade to gray]").arg(cmd) << grayLevel << duration;
			break;
		}
		case 10: { /* fade to black */
			duration = light_program_next_byte(index, u8data);
			fadeToGray(0, duration);
			//qDebug() << QString("[cmd:%1 fade to black]").arg(cmd) << duration;
			break;
		}
		case 11: { /* fade to white */
			duration = light_program_next_byte(index, u8data);
			fadeToGray(255, duration);
			//qDebug() << QString("[cmd:%1 fade to white]").arg(cmd) << duration;
			break;
		}
		case 12: { /* loop begin */
			iterations = light_program_next_byte(index, u8data);
			loopStack.push_back({ index, iterations > 0 ? iterations : INT_MAX });
			//qDebug() << QString("[cmd:%1 loop begin]").arg(cmd) << iterations;
			break;
		}
		case 13: { /* loop end */
			if (loopStack.empty()) {
				assert(!loopStack.empty());
				return Status::Corruption("Found end loop command when loop stack is empty");
			}

			const auto& te_pair = loopStack.rbegin();
			if (--(te_pair->second) > 0)	index = te_pair->first;
			else							loopStack.pop_back();


			break;
		}
		case 20:
		case 21: { /* set pyro / set pyro all */
			/* just read the next byte that belongs to the channel, but don't
			   do anything with it */
			light_program_next_byte(index, u8data);
			break;
		}

		default: {
			assert(false);
			return Status::NotSupported("Unknown command in light program");

		}
		}// case

		//TODO check duraion == te_endTime - te_timeStamp
    	if ( duration > 0 ) _segment.emplace_back(te_timestamp, te_endTime, te_isFade, te_startColor, te_endColor,duration);
	}

	// test
	// for (auto it : _segment) {
	// 	std::cout << " fade:" << it.isFade
	// 		<< " start:" << it.startTime
	// 		<< " end" << it.endTime
	// 		<< " du:" << it.duration
	// 		<< " start:" << it.startColor.toString()
	// 		<< " end:"  << it.endColor.toString() << std::endl;
	// }

	_init = true;
	return Status::OK();
}

Status LightProgram::getColorAt(uint32_t time, Color &color)
{
	if ( !_init ) {
		return Status::NotSupported("not init light program");
	}

	auto index = _getSegmentIndex(time);

	if ( index == -1 ) {
		color = {};
	} else {
		// TODO 适配起飞结束
		color = _segment[index].getColorAt(time);
	}

	return Status::OK();
}

int LightProgram::_getSegmentIndex(uint32_t time)
{
	auto comp = [](const LightSegment &seg, uint32_t val) {
		return seg.endTime < val;
	};

	auto it = std::lower_bound(_segment.begin(), _segment.end(), time, comp);
	if ( it == _segment.end() ) return -1;
	return  std::distance(_segment.begin(), it);
}

}
