// SPDX-License-Identifier: GPL-2.0
#include <errno.h>

#include <QtBluetooth/QBluetoothAddress>
#include <QLowEnergyController>
#include <QLowEnergyService>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QThread>
#include <QTimer>
#include <QTime>
#include <QDebug>
#include <QLoggingCategory>

#include <libdivecomputer/version.h>

#include "libdivecomputer.h"
#include "core/qt-ble.h"
#include "core/btdiscovery.h"
#include "core/subsurface-string.h"

#define BLE_TIMEOUT 12000 // 12 seconds seems like a very long time to wait
#define DEBUG_THRESHOLD 50
static int debugCounter;

#define IS_HW(_d) same_string((_d)->vendor, "Heinrichs Weikamp")
#define IS_SHEARWATER(_d) same_string((_d)->vendor, "Shearwater")
#define IS_GARMIN(_d) same_string((_d)->vendor, "Garmin")

#define MAXIMAL_HW_CREDIT	255
#define MINIMAL_HW_CREDIT	32

#define WAITFOR(expression, ms) do {					\
	Q_ASSERT(QCoreApplication::instance());				\
	Q_ASSERT(QThread::currentThread());				\
									\
	if (expression)							\
		break;							\
	QElapsedTimer timer;						\
	timer.start();							\
									\
	do {								\
		QCoreApplication::processEvents(QEventLoop::AllEvents, ms); \
		if (expression)						\
			break;						\
		QThread::msleep(10);					\
	} while (timer.elapsed() < (ms));				\
} while (0)

extern "C" {

void BLEObject::serviceStateChanged(QLowEnergyService::ServiceState newState)
{
	qDebug() << "serviceStateChanged";
	auto service = qobject_cast<QLowEnergyService*>(sender());
	if (service)
		qDebug() << service->serviceUuid() << newState;
}

void BLEObject::characteristcStateChanged(const QLowEnergyCharacteristic &c, const QByteArray &value)
{
	qDebug() << QTime::currentTime() << "packet RECV" << value.toHex();
	if (IS_HW(device)) {
		if (c.uuid() == hwAllCharacteristics[HW_OSTC_BLE_DATA_TX]) {
			hw_credit--;
			receivedPackets.append(value);
			if (hw_credit == MINIMAL_HW_CREDIT)
				setHwCredit(MAXIMAL_HW_CREDIT - MINIMAL_HW_CREDIT);
		} else {
			qDebug() << "ignore packet from" << c.uuid() << value.toHex();
		}
	} else {
		receivedPackets.append(value);
	}
}

void BLEObject::characteristicWritten(const QLowEnergyCharacteristic &c, const QByteArray &value)
{
	if (IS_HW(device)) {
		if (c.uuid() == hwAllCharacteristics[HW_OSTC_BLE_CREDITS_RX]) {
			bool ok;
			hw_credit += value.toHex().toInt(&ok, 16);
			isCharacteristicWritten = true;
		}
	} else {
		if (debugCounter < DEBUG_THRESHOLD)
			qDebug() << "BLEObject::characteristicWritten";
	}
}

void BLEObject::writeCompleted(const QLowEnergyDescriptor&, const QByteArray&)
{
	qDebug() << "BLE write completed";
}

void BLEObject::addService(const QBluetoothUuid &newService)
{
	qDebug() << "Found service" << newService;

	auto service = controller->createServiceObject(newService, this);
	qDebug() << " .. created service object" << service;
	if (service) {
		services.append(service);
		service->discoverDetails();
	}
}

BLEObject::BLEObject(QLowEnergyController *c, dc_user_device_t *d)
{
	controller = c;
	device = d;
	debugCounter = 0;
	isCharacteristicWritten = false;
}

BLEObject::~BLEObject()
{
	qDebug() << "Deleting BLE object";

	foreach (QLowEnergyService *service, services)
		delete service;

	delete controller;
}

// a write characteristic needs Write or WriteNoResponse
static bool is_write_characteristic(const QLowEnergyCharacteristic &c)
{
	return c.properties() &
		 (QLowEnergyCharacteristic::Write |
		  QLowEnergyCharacteristic::WriteNoResponse);
}

// We need a Notify or Indicate for the reading side, and
// a descriptor to enable it
static bool is_read_characteristic(const QLowEnergyCharacteristic &c)
{
	return !c.descriptors().empty() &&
		(c.properties() &
		  (QLowEnergyCharacteristic::Notify |
		   QLowEnergyCharacteristic::Indicate));
}

dc_status_t BLEObject::write(const void *data, size_t size, size_t *actual)
{
	if (actual) *actual = 0;

	if (!receivedPackets.isEmpty()) {
		qDebug() << ".. write HIT with still incoming packets in queue";
		do {
			receivedPackets.takeFirst();
		} while (!receivedPackets.isEmpty());
	}

	foreach (const QLowEnergyCharacteristic &c, preferredService()->characteristics()) {
		if (!is_write_characteristic(c))
			continue;

		QByteArray bytes((const char *)data, (int) size);
		qDebug() << QTime::currentTime() << "packet SEND" << bytes.toHex();

		QLowEnergyService::WriteMode mode;
		mode = (c.properties() & QLowEnergyCharacteristic::WriteNoResponse) ?
				QLowEnergyService::WriteWithoutResponse :
				QLowEnergyService::WriteWithResponse;

		preferredService()->writeCharacteristic(c, bytes, mode);
		if (actual) *actual = size;
		return DC_STATUS_SUCCESS;
	}

	return DC_STATUS_IO;
}

dc_status_t BLEObject::read(void *data, size_t size, size_t *actual)
{
	if (actual)
		*actual = 0;
	if (receivedPackets.isEmpty()) {
		QList<QLowEnergyCharacteristic> list = preferredService()->characteristics();
		if (list.isEmpty())
			return DC_STATUS_IO;

		qDebug() << QTime::currentTime() << "packet WAIT";

		WAITFOR(!receivedPackets.isEmpty(), BLE_TIMEOUT);
		if (receivedPackets.isEmpty())
			return DC_STATUS_IO;
	}

	QByteArray packet = receivedPackets.takeFirst();

	// Did we get more than asked for?
	//
	// Put back the left-over at the beginning of the
	// received packet list, and truncate the packet
	// we got to just the part asked for.
	if ((size_t)packet.size() > size) {
		receivedPackets.prepend(packet.mid(size));
		packet.truncate(size);
	}

	memcpy((char *)data, packet.data(), packet.size());
	if (actual)
		*actual += packet.size();

	qDebug() << QTime::currentTime() << "packet READ" << packet.toHex();

	return DC_STATUS_SUCCESS;
}

//
// select_preferred_service() gets called after all services
// have been discovered, and the discovery process has been
// started (by addService(), which calls service->discoverDetails())
//
// The role of this function is to wait for all service
// discovery to finish, and pick the preferred service.
//
// NOTE! Picking the preferred service is divecomputer-specific.
// Right now we special-case the HW known service number, but for
// others we just pick the first one that isn't a standard service.
//
// That's wrong, but works for the simple case.
//
dc_status_t BLEObject::select_preferred_service(void)
{
	// Wait for each service to finish discovering
	foreach (const QLowEnergyService *s, services) {
		WAITFOR(s->state() != QLowEnergyService::DiscoveringServices, BLE_TIMEOUT);
	}

	// Print out the services for debugging
	foreach (const QLowEnergyService *s, services) {
		qDebug() << "Found service" << s->serviceUuid() << s->serviceName();

		foreach (const QLowEnergyCharacteristic &c, s->characteristics()) {
			qDebug() << "   c:" << c.uuid();

			foreach (const QLowEnergyDescriptor &d, c.descriptors())
				qDebug() << "        d:" << d.uuid();
		}
	}

	// Pick the preferred one
	foreach (QLowEnergyService *s, services) {
		if (s->state() != QLowEnergyService::ServiceDiscovered)
			continue;

		bool isStandardUuid = false;
		QBluetoothUuid uuid = s->serviceUuid();

		uuid.toUInt16(&isStandardUuid);

		if (IS_HW(device)) {
			/* The HW BT/BLE piece or hardware uses, what we
			 * call here, "a Standard UUID. It is standard because the Telit/Stollmann
			 * manufacturer applied for an own UUID for its product, and this was granted
			 * by the Bluetooth SIG.
			 */
			if (uuid != QUuid("{0000fefb-0000-1000-8000-00805f9b34fb}"))
				continue; // skip all services except the right one
		} else if (isStandardUuid) {
			qDebug () << " .. ignoring standard service" << uuid;
			continue;
		} else {
			bool hasread = false;
			bool haswrite = false;

			foreach (const QLowEnergyCharacteristic &c, s->characteristics()) {
				hasread |= is_read_characteristic(c);
				haswrite |= is_write_characteristic(c);
			}

			if (!hasread) {
				qDebug () << " .. ignoring service without read characteristic" << uuid;
				continue;
			}
			if (!haswrite) {
				qDebug () << " .. ignoring service without write characteristic" << uuid;
				continue;
			}
		}

		// We now know that the service has both read and write characteristics
		preferred = s;
		qDebug() << "Using service" << s->serviceUuid() << "as preferred service";
		break;
	}

	if (!preferred) {
		qDebug() << "failed to find suitable service";
		report_error("Failed to find suitable BLE GATT service");
		return DC_STATUS_IO;
	}

	connect(preferred, &QLowEnergyService::stateChanged, this, &BLEObject::serviceStateChanged);
	connect(preferred, &QLowEnergyService::characteristicChanged, this, &BLEObject::characteristcStateChanged);
	connect(preferred, &QLowEnergyService::characteristicWritten, this, &BLEObject::characteristicWritten);
	connect(preferred, &QLowEnergyService::descriptorWritten, this, &BLEObject::writeCompleted);

	return DC_STATUS_SUCCESS;
}

dc_status_t BLEObject::setHwCredit(unsigned int c)
{
	/* The Terminal I/O client transmits initial UART credits to the server (see 6.5).
	 *
	 * Notice that we have to write to the characteristic here, and not to its
	 * descriptor as for the enabeling of notifications or indications.
	 *
	 * Futher notice that this function has the implicit effect of processing the
	 * event loop (due to waiting for the confirmation of the credit request).
	 * So, as characteristcStateChanged will be triggered, while receiving
	 * data from the OSTC, these are processed too.
	 */

	QList<QLowEnergyCharacteristic> list = preferredService()->characteristics();
	isCharacteristicWritten = false;
	preferredService()->writeCharacteristic(list[HW_OSTC_BLE_CREDITS_RX],
						QByteArray(1, c),
						QLowEnergyService::WriteWithResponse);

	/* And wait for the answer*/
	WAITFOR(isCharacteristicWritten, BLE_TIMEOUT);

	if (!isCharacteristicWritten)
		return DC_STATUS_TIMEOUT;
	return DC_STATUS_SUCCESS;
}

dc_status_t BLEObject::setupHwTerminalIo(QList<QLowEnergyCharacteristic> allC)
{	/* This initalizes the Terminal I/O client as described in
	 * http://www.telit.com/fileadmin/user_upload/products/Downloads/sr-rf/BlueMod/TIO_Implementation_Guide_r04.pdf
	 * Referenced section numbers below are from that document.
	 *
	 * This is for all HW computers, that use referenced BT/BLE hardware module from Telit
	 * (formerly Stollmann). The 16 bit UUID 0xFEFB (or a derived 128 bit UUID starting with
	 * 0x0000FEFB is a clear indication that the OSTC is equipped with this BT/BLE hardware.
	 */

	if (allC.length() != 4) {
		qDebug() << "This should not happen. HW/OSTC BT/BLE device without 4 Characteristics";
		return DC_STATUS_IO;
	}

	/* The Terminal I/O client subscribes to indications of the UART credits TX
	 * characteristic (see 6.4).
	 *
	 * Notice that indications are subscribed to by writing 0x0200 to its descriptor. This
	 * can be understood by looking for Client Characteristic Configuration, Assigned
	 * Number: 0x2902. Enabling/Disabeling is setting the proper bit, and they
	 * differ for indications and notifications.
	 */
	QLowEnergyDescriptor d = allC[HW_OSTC_BLE_CREDITS_TX].descriptors().first();
	preferredService()->writeDescriptor(d, QByteArray::fromHex("0200"));

	/* The Terminal I/O client subscribes to notifications of the UART data TX
	 * characteristic (see 6.2).
	 */
	d = allC[HW_OSTC_BLE_DATA_TX].descriptors().first();
	preferredService()->writeDescriptor(d, QByteArray::fromHex("0100"));

	/* The Terminal I/O client transmits initial UART credits to the server (see 6.5). */
	return setHwCredit(MAXIMAL_HW_CREDIT);
}

#if !defined(Q_OS_WIN)
// Bluez is broken, and doesn't have a sane way to query
// whether to use a random address or not. So we have to
// fake it.
static int use_random_address(dc_user_device_t *user_device)
{
	return IS_SHEARWATER(user_device) || IS_GARMIN(user_device);
}
#endif

dc_status_t qt_ble_open(void **io, dc_context_t *, const char *devaddr, dc_user_device_t *user_device)
{
	debugCounter = 0;
	QLoggingCategory::setFilterRules(QStringLiteral("qt.bluetooth* = true"));

	/*
	 * LE-only devices get the "LE:" prepended by the scanning
	 * code, so that the rfcomm code can see they only do LE.
	 *
	 * We just skip that prefix (and it doesn't always exist,
	 * since the device may support both legacy BT and LE).
	 */
	if (!strncmp(devaddr, "LE:", 3))
		devaddr += 3;

	// HACK ALERT! Qt 5.9 needs this for proper Bluez operation
	qputenv("QT_DEFAULT_CENTRAL_SERVICES", "1");

#if defined(Q_OS_MACOS) || defined(Q_OS_IOS)
	QBluetoothDeviceInfo remoteDevice = getBtDeviceInfo(QString(devaddr));
	QLowEnergyController *controller = QLowEnergyController::createCentral(remoteDevice);
#else
	// this is deprecated but given that we don't use Qt to scan for
	// devices on Android, we don't have QBluetoothDeviceInfo for the
	// paired devices and therefore cannot use the newer interfaces
	// that are preferred starting with Qt 5.7
	QBluetoothAddress remoteDeviceAddress(devaddr);
	QLowEnergyController *controller = new QLowEnergyController(remoteDeviceAddress);
#endif
	qDebug() << "qt_ble_open(" << devaddr << ")";

#if !defined(Q_OS_WIN)
	if (use_random_address(user_device))
		controller->setRemoteAddressType(QLowEnergyController::RandomAddress);
#endif

	// Try to connect to the device
	controller->connectToDevice();

	// Create a timer. If the connection doesn't succeed after five seconds or no error occurs then stop the opening step
	WAITFOR(controller->state() != QLowEnergyController::ConnectingState, BLE_TIMEOUT);

	switch (controller->state()) {
	case QLowEnergyController::ConnectedState:
		qDebug() << "connected to the controller for device" << devaddr;
		break;
	case QLowEnergyController::ConnectingState:
		qDebug() << "timeout while trying to connect to the controller " << devaddr;
		report_error("Timeout while trying to connect to %s", devaddr);
		delete controller;
		return DC_STATUS_IO;
	default:
		qDebug() << "failed to connect to the controller " << devaddr << "with error" << controller->errorString();
		report_error("Failed to connect to %s: '%s'", devaddr, qPrintable(controller->errorString()));
		delete controller;
		return DC_STATUS_IO;
	}

	// We need to discover services etc here!
	// Note that ble takes ownership of controller and henceforth deleting ble will
	// take care of deleting controller.
	BLEObject *ble = new BLEObject(controller, user_device);
	ble->connect(controller, SIGNAL(serviceDiscovered(QBluetoothUuid)), SLOT(addService(QBluetoothUuid)));

	qDebug() << "  .. discovering services";

	controller->discoverServices();

	WAITFOR(controller->state() != QLowEnergyController::DiscoveringState, BLE_TIMEOUT);

	qDebug() << " .. done discovering services";

	dc_status_t error = ble->select_preferred_service();

	if (error != DC_STATUS_SUCCESS) {
		qDebug() << "failed to find suitable service on" << devaddr;
		report_error("Failed to find suitable service on '%s'", devaddr);
		delete ble;
		return error;
	}

	qDebug() << " .. enabling notifications";

	/* Enable notifications */
	QList<QLowEnergyCharacteristic> list = ble->preferredService()->characteristics();
	if (IS_HW(user_device)) {
		dc_status_t r = ble->setupHwTerminalIo(list);
		if (r != DC_STATUS_SUCCESS) {
			delete ble;
			return r;
		}
	} else {
		foreach (const QLowEnergyCharacteristic &c, list) {
			if (!is_read_characteristic(c))
				continue;

			qDebug() << "Using read characteristic" << c.uuid();

			QList<QLowEnergyDescriptor> l = c.descriptors();
			QLowEnergyDescriptor d = l.first();

			foreach (const QLowEnergyDescriptor &tmp, l) {
				if (tmp.type() == QBluetoothUuid::ClientCharacteristicConfiguration) {
					d = tmp;
					break;
				}
			}

			qDebug() << "now writing \"0x0100\" to the descriptor" << d.uuid().toString();

			ble->preferredService()->writeDescriptor(d, QByteArray::fromHex("0100"));
			break;
		}
	}

	// Fill in info
	*io = (void *)ble;
	return DC_STATUS_SUCCESS;
}

dc_status_t qt_ble_close(void *io)
{
	BLEObject *ble = (BLEObject *) io;

	delete ble;

	return DC_STATUS_SUCCESS;
}
static void checkThreshold()
{
	if (++debugCounter == DEBUG_THRESHOLD) {
		QLoggingCategory::setFilterRules(QStringLiteral("qt.bluetooth* = false"));
		qDebug() << "turning off further BT debug output";
	}
}

dc_status_t qt_ble_read(void *io, void* data, size_t size, size_t *actual)
{
	checkThreshold();
	BLEObject *ble = (BLEObject *) io;
	return ble->read(data, size, actual);
}

dc_status_t qt_ble_write(void *io, const void* data, size_t size, size_t *actual)
{
	checkThreshold();
	BLEObject *ble = (BLEObject *) io;
	return ble->write(data, size, actual);
}

} /* extern "C" */
