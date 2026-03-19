#include "MeterService.h"

MeterService::MeterService(IMeterReader& reader, IPublisher& publisher, IDisplay& display)
    : _reader(reader), _publisher(publisher), _display(display) {}

bool MeterService::update(bool meterConnected) {
    _publisher.loop();

    if (!meterConnected) return false;
    if (!_reader.poll()) return false;

    const MeterData& data = _reader.getData();
    _publisher.publish(data);
    _display.showStatus(true, data, _publisher.isConnected());
    return true;
}
