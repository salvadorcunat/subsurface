// SPDX-License-Identifier: GPL-2.0
/*
 * models.cpp
 *
 * classes for the equipment models of Subsurface
 *
 */
#include "qt-models/models.h"
#include "core/helpers.h"

#include <QLocale>

// initialize the trash icon if necessary

const QPixmap &trashIcon()
{
	static QPixmap trash = QPixmap(":list-remove-icon").scaledToHeight(defaultIconMetrics().sz_small);
	return trash;
}

const QPixmap &trashForbiddenIcon()
{
	static QPixmap trash = QPixmap(":list-remove-disabled-icon").scaledToHeight(defaultIconMetrics().sz_small);
	return trash;
}

Qt::ItemFlags GasSelectionModel::flags(const QModelIndex &index) const
{
	Q_UNUSED(index);
	return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}

GasSelectionModel *GasSelectionModel::instance()
{
	static GasSelectionModel self;
	return &self;
}

//TODO: Remove this #include here when the issue below is fixed.
#include "diveplannermodel.h"
void GasSelectionModel::repopulate()
{
	/* TODO:
	 * getGasList shouldn't be a member of DivePlannerPointsModel,
	 * it has nothing to do with the current plain being calculated:
	 * it's internal to the current_dive.
	 */
	setStringList(DivePlannerPointsModel::instance()->getGasList());
}

QVariant GasSelectionModel::data(const QModelIndex &index, int role) const
{
	if (role == Qt::FontRole) {
		return defaultModelFont();
	}
	return QStringListModel::data(index, role);
}

// Language Model, The Model to populate the list of possible Languages.

LanguageModel *LanguageModel::instance()
{
	static LanguageModel *self = new LanguageModel();
	QLocale l;
	return self;
}

LanguageModel::LanguageModel(QObject *parent) : QAbstractListModel(parent)
{
	QDir d(getSubsurfaceDataPath("translations"));
	Q_FOREACH (const QString &s, d.entryList()) {
		if (s.startsWith("subsurface_") && s.endsWith(".qm")) {
			languages.push_back((s == "subsurface_source.qm") ? "English" : s);
		}
	}
}

QVariant LanguageModel::data(const QModelIndex &index, int role) const
{
	QLocale loc;
	QString currentString = languages.at(index.row());
	if (!index.isValid())
		return QVariant();
	switch (role) {
	case Qt::DisplayRole: {
		QLocale l(currentString.remove("subsurface_").remove(".qm"));
		return currentString == "English" ? currentString : QString("%1 (%2)").arg(l.languageToString(l.language())).arg(l.countryToString(l.country()));
	}
	case Qt::UserRole:
		return currentString == "English" ? "en_US" : currentString.remove("subsurface_").remove(".qm");
	}
	return QVariant();
}

int LanguageModel::rowCount(const QModelIndex &parent) const
{
	Q_UNUSED(parent);
	return languages.count();
}
