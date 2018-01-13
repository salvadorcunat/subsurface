// SPDX-License-Identifier: GPL-2.0
#include "divecomputer.h"
#include "dive.h"
#include "subsurface-qt/SettingsObjectWrapper.h"

DiveComputerList dcList;

DiveComputerList::DiveComputerList()
{
}

DiveComputerList::~DiveComputerList()
{
}

bool DiveComputerNode::operator==(const DiveComputerNode &a) const
{
	return model == a.model &&
	       deviceId == a.deviceId &&
	       firmware == a.firmware &&
	       serialNumber == a.serialNumber &&
	       nickName == a.nickName;
}

bool DiveComputerNode::operator!=(const DiveComputerNode &a) const
{
	return !(*this == a);
}

bool DiveComputerNode::changesValues(const DiveComputerNode &b) const
{
	if (model != b.model || deviceId != b.deviceId) {
		qDebug("DiveComputerNodes were not for the same DC");
		return false;
	}
	return (firmware != b.firmware) ||
	       (serialNumber != b.serialNumber) ||
	       (nickName != b.nickName);
}

const DiveComputerNode *DiveComputerList::getExact(const QString &m, uint32_t d)
{
	for (QMap<QString, DiveComputerNode>::iterator it = dcMap.find(m); it != dcMap.end() && it.key() == m; ++it)
		if (it->deviceId == d)
			return &*it;
	return NULL;
}

const DiveComputerNode *DiveComputerList::get(const QString &m)
{
	QMap<QString, DiveComputerNode>::iterator it = dcMap.find(m);
	if (it != dcMap.end())
		return &*it;
	return NULL;
}

void DiveComputerNode::showchanges(const QString &n, const QString &s, const QString &f) const
{
	if (nickName != n)
		qDebug("new nickname %s for DC model %s deviceId 0x%x", n.toUtf8().data(), model.toUtf8().data(), deviceId);
	if (serialNumber != s)
		qDebug("new serial number %s for DC model %s deviceId 0x%x", s.toUtf8().data(), model.toUtf8().data(), deviceId);
	if (firmware != f)
		qDebug("new firmware version %s for DC model %s deviceId 0x%x", f.toUtf8().data(), model.toUtf8().data(), deviceId);
}

void DiveComputerList::addDC(QString m, uint32_t d, QString n, QString s, QString f)
{
	if (m.isEmpty() || d == 0)
		return;
	const DiveComputerNode *existNode = this->getExact(m, d);

	if (existNode) {
		// Update any non-existent fields from the old entry
		if (n.isEmpty())
			n = existNode->nickName;
		if (s.isEmpty())
			s = existNode->serialNumber;
		if (f.isEmpty())
			f = existNode->firmware;

		// Do all the old values match?
		if (n == existNode->nickName && s == existNode->serialNumber && f == existNode->firmware)
			return;

		// debugging: show changes
		if (verbose)
			existNode->showchanges(n, s, f);
		dcMap.remove(m, *existNode);
	}

	DiveComputerNode newNode(m, d, s, f, n);
	dcMap.insert(m, newNode);
}

extern "C" void create_device_node(const char *model, uint32_t deviceid, const char *serial, const char *firmware, const char *nickname)
{
	dcList.addDC(model, deviceid, nickname, serial, firmware);
}

extern "C" bool compareDC(const DiveComputerNode &a, const DiveComputerNode &b)
{
	return a.deviceId < b.deviceId;
}

extern "C" void call_for_each_dc (void *f, void (*callback)(void *, const char *, uint32_t,
							   const char *, const char *, const char *),
				  bool select_only)
{
	QList<DiveComputerNode> values = dcList.dcMap.values();
	qSort(values.begin(), values.end(), compareDC);
	for (int i = 0; i < values.size(); i++) {
		const DiveComputerNode *node = &values.at(i);
		bool found = false;
		if (select_only) {
			int j;
			struct dive *d;
			for_each_dive (j, d) {
				struct divecomputer *dc;
				if (!d->selected)
					continue;
				for_each_dc(d, dc) {
					if (dc->deviceid == node->deviceId) {
						found = true;
						break;
					}
				}
				if (found)
					break;
			}
		} else {
			found = true;
		}
		if (found)
			callback(f, node->model.toUtf8().data(), node->deviceId, node->nickName.toUtf8().data(),
				 node->serialNumber.toUtf8().data(), node->firmware.toUtf8().data());
	}
}


extern "C" int is_default_dive_computer(const char *vendor, const char *product)
{
	auto dc = SettingsObjectWrapper::instance()->dive_computer_settings;
	return dc->dc_vendor() == vendor && dc->dc_product() == product;
}

extern "C" int is_default_dive_computer_device(const char *name)
{
	auto dc = SettingsObjectWrapper::instance()->dive_computer_settings;
	return dc->dc_device() == name;
}

extern "C" void set_dc_nickname(struct dive *dive)
{
	if (!dive)
		return;

	struct divecomputer *dc;

	for_each_dc (dive, dc) {
		if (!empty_string(dc->model) && dc->deviceid &&
		    !dcList.getExact(dc->model, dc->deviceid)) {
			// we don't have this one, yet
			const DiveComputerNode *existNode = dcList.get(dc->model);
			if (existNode) {
				// we already have this model but a different deviceid
				QString simpleNick(dc->model);
				if (dc->deviceid == 0)
					simpleNick.append(" (unknown deviceid)");
				else
					simpleNick.append(" (").append(QString::number(dc->deviceid, 16)).append(")");
				dcList.addDC(dc->model, dc->deviceid, simpleNick);
			} else {
				dcList.addDC(dc->model, dc->deviceid);
			}
		}
	}
}
