// SPDX-License-Identifier: GPL-2.0
import QtQuick 2.6
import QtQuick.Controls 2.0
import QtQuick.Controls.Material 2.1
import QtQuick.Window 2.2
import QtQuick.Dialogs 1.2
import QtQuick.Layouts 1.2
import QtQuick.Window 2.2
import org.subsurfacedivelog.mobile 1.0
import org.kde.kirigami 2.4 as Kirigami
import QtGraphicalEffects 1.0
import QtQuick.Templates 2.0 as QtQuickTemplates

Kirigami.ApplicationWindow {
	id: rootItem
	title: qsTr("Subsurface-mobile")
	reachableModeEnabled: false // while it's a good idea, it seems to confuse more than help
	wideScreen: false // workaround for probably Kirigami bug. See commits.

	// the documentation claims that the ApplicationWindow should pick up the font set on
	// the C++ side. But as a matter of fact, it doesn't, unless you add this line:
	font: Qt.application.font

	pageStack.globalToolBar.style: Kirigami.ApplicationHeaderStyle.Breadcrumb
	pageStack.globalToolBar.showNavigationButtons: (Kirigami.ApplicationHeaderStyle.ShowBackButton | Kirigami.ApplicationHeaderStyle.ShowForwardButton)
	pageStack.globalToolBar.minimumHeight: 0
	pageStack.globalToolBar.preferredHeight: Math.round(Kirigami.Units.gridUnit * (Qt.platform.os == "ios" ? 2 : 1.5))
	pageStack.globalToolBar.maximumHeight: Kirigami.Units.gridUnit * 2

	property alias notificationText: manager.notificationText
	property alias locationServiceEnabled: manager.locationServiceEnabled
	property alias pluggedInDeviceName: manager.pluggedInDeviceName
	property alias defaultCylinderIndex: settingsWindow.defaultCylinderIndex
	property bool filterToggle: false
	property string filterPattern: ""
	property int colWidth: undefined

	onNotificationTextChanged: {
		// once the app is fully initialized and the UI is running, we use passive
		// notifications to show the notification text, but during initialization
		// we instead dump the information into the textBlock below - and to make
		// this visually more useful we interpret a "\r" at the beginning of a notification
		// to mean that we want to simply over-write the last line, not create a new one
		if (initialized) {
			// make sure any old notification is hidden
			hidePassiveNotification()
			if (notificationText !== "") {
				// there's a risk that we have a >5 second gap in update events;
				// still, keep the timeout at 5s to avoid odd unchanging notifications
				showPassiveNotification(notificationText, 5000)
			}
		} else {
			var oldText = textBlock.text
			if (notificationText.startsWith("\r")) {
				// replace the last line that was sent
				oldText = oldText.substr(0, oldText.lastIndexOf("\n"))
				textBlock.text = oldText + "\n" + notificationText.substr(1)
			} else {
				textBlock.text = oldText + "\n" + notificationText
			}
		}
	}
	visible: false

	BusyIndicator {
		id: busy
		running: false
		height: 6 * Kirigami.Units.gridUnit
		width: 6 * Kirigami.Units.gridUnit
		anchors.centerIn: parent
	}

	function showBusy(msg) {
		if (msg !== undefined && msg !== "")
			showPassiveNotification(msg, 15000) // show for 15 seconds
		busy.running = true
	}

	function hideBusy() {
		busy.running = false
		hidePassiveNotification()
	}

	function returnTopPage() {
		for (var i=pageStack.depth; i>1; i--) {
			pageStack.pop()
		}
		detailsWindow.endEditMode()
	}

	function scrollToTop() {
		diveList.scrollToTop()
	}

	function showPage(page) {
		if (page !== mapPage)
			hackToOpenMap = 0 // we really want a different page
		if (globalDrawer.drawerOpen)
			globalDrawer.close()
		var i=pageIndex(page)
		if (i === -1)
			pageStack.push(page)
		else
			pageStack.currentIndex = i
		manager.appendTextToLog("switched to page " + page.title)
	}

	function showMap() {
		showPage(mapPage)
	}

	function showDiveList() {
		showPage(diveList)
	}

	function pageIndex(pageToFind) {
		if (pageStack.contentItem !== null) {
			for (var i = 0; i < pageStack.contentItem.contentChildren.length; i++) {
				if (pageStack.contentItem.contentChildren[i] === pageToFind)
					return i
			}
		}
		return -1
	}

	function startAddDive() {
		detailsWindow.state = "add"
		detailsWindow.dive_id = manager.addDive();
		detailsWindow.number = manager.getNumber(detailsWindow.dive_id)
		detailsWindow.date = manager.getDate(detailsWindow.dive_id)
		detailsWindow.airtemp = ""
		detailsWindow.watertemp = ""
		detailsWindow.buddyModel = manager.buddyList
		detailsWindow.buddyIndex = -1
		detailsWindow.buddyText = ""
		detailsWindow.depth = ""
		detailsWindow.divemasterModel = manager.divemasterList
		detailsWindow.divemasterIndex = -1
		detailsWindow.divemasterText = ""
		detailsWindow.notes = ""
		detailsWindow.location = ""
		detailsWindow.gps = ""
		detailsWindow.duration = ""
		detailsWindow.suitModel = manager.suitList
		detailsWindow.suitIndex = -1
		detailsWindow.suitText = ""
		detailsWindow.cylinderModel0 = manager.cylinderInit
		detailsWindow.cylinderModel1 = manager.cylinderInit
		detailsWindow.cylinderModel2 = manager.cylinderInit
		detailsWindow.cylinderModel3 = manager.cylinderInit
		detailsWindow.cylinderModel4 = manager.cylinderInit
		detailsWindow.cylinderIndex0 = PrefEquipment.default_cylinder == "" ? -1 : detailsWindow.cylinderModel0.indexOf(PrefEquipment.default_cylinder)
		detailsWindow.usedCyl = ["",]
		detailsWindow.weight = ""
		detailsWindow.usedGas = []
		detailsWindow.startpressure = []
		detailsWindow.endpressure = []
		detailsWindow.gpsCheckbox = false
		showPage(detailsWindow)
	}

	contextDrawer: Kirigami.ContextDrawer {
		id: contextDrawer
		closePolicy: QtQuickTemplates.Popup.CloseOnPressOutside
	}

	globalDrawer: Kirigami.GlobalDrawer {
		id: globalDrawer
		height: rootItem.height
		rightPadding: 0
		enabled: (Backend.cloud_verification_status === Enums.CS_NOCLOUD ||
				  Backend.cloud_verification_status === Enums.CS_VERIFIED)
		topContent: Image {
			source: "qrc:/qml/icons/dive.jpg"
			Layout.fillWidth: true
			sourceSize.width: parent.width
			fillMode: Image.PreserveAspectFit
			LinearGradient {
				anchors {
					left: parent.left
					right: parent.right
					top: parent.top
				}
				height: textblock.height * 2
				start: Qt.point(0, 0)
				end: Qt.point(0, height)
				gradient: Gradient {
					GradientStop {
						position: 0.0
						color: Qt.rgba(0, 0, 0, 0.8)
					}
					GradientStop {
						position: 1.0
						color: "transparent"
					}
				}
			}
			ColumnLayout {
				id: textblock
				anchors {
					left: parent.left
					top: parent.top
				}
				RowLayout {
					width: Math.min(implicitWidth, parent.width)
					Layout.margins: Kirigami.Units.smallSpacing
					Image {
						source: "qrc:/qml/subsurface-mobile-icon.png"
						fillMode: Image.PreserveAspectCrop
						sourceSize.width: Kirigami.Units.iconSizes.large
						width: Kirigami.Units.iconSizes.large
						Layout.margins: Kirigami.Units.smallSpacing
					}
					Kirigami.Heading {
						Layout.fillWidth: true
						visible: text.length > 0
						level: 1
						color: "white"
						text: "Subsurface"
						wrapMode: Text.NoWrap
						elide: Text.ElideRight
						font.weight: Font.Normal
						Layout.margins: Kirigami.Units.smallSpacing
					}
				}
				RowLayout {
					Layout.margins: Kirigami.Units.smallSpacing
					Kirigami.Heading {
						Layout.fillWidth: true
						visible: text.length > 0
						level: 3
						color: "white"
						text: PrefCloudStorage.cloud_storage_email
						wrapMode: Text.NoWrap
						elide: Text.ElideRight
						font.weight: Font.Normal
					}
				}
			}
		}

		resetMenuOnTriggered: false

		actions: [
			Kirigami.Action {
				icon {
					name: ":/icons/ic_home.svg"
				}
				text: qsTr("Dive list")
				onTriggered: {
					manager.appendTextToLog("requested dive list with credential status " + Backend.cloud_verification_status)
					returnTopPage()
					globalDrawer.close()
				}
			},
			Kirigami.Action {
				icon {
					name: ":/icons/ic_sync.svg"
				}
				text: qsTr("Dive management")
				Kirigami.Action {
					icon {
						name: ":/go-previous-symbolic"
					}
					text: qsTr("Back")
					onTriggered: globalDrawer.scrollViewItem.pop()
				}
				Kirigami.Action {
					icon {
						name: ":/icons/ic_add.svg"
					}
					text: qsTr("Add dive manually")
					onTriggered: {
						globalDrawer.close()
						returnTopPage()  // otherwise odd things happen with the page stack
						startAddDive()
					}
				}
				Kirigami.Action {
					// this of course assumes a white background - theming means this needs to change again
					icon {
						name: ":/icons/downloadDC-black.svg"
					}
					text: qsTr("Download from DC")
					enabled: true
					onTriggered: {
						globalDrawer.close()
						downloadFromDc.dcImportModel.clearTable()
						showPage(downloadFromDc)
					}
				}
				Kirigami.Action {
					icon {
						name: ":/icons/ic_add_location.svg"
					}
					text: qsTr("Apply GPS fixes")
					onTriggered: {
						globalDrawer.close()
						showBusy()
						diveList.diveListModel = null
						manager.applyGpsData()
						diveModel.resetInternalData()
						manager.refreshDiveList()
						while (pageStack.depth > 1) {
							pageStack.pop()
						}
						diveList.diveListModel = diveModel
						showDiveList()
						hideBusy()
					}
				}
				Kirigami.Action {
					icon {
						name: ":/icons/cloud_sync.svg"
					}
					text: qsTr("Manual sync with cloud")
					enabled: Backend.cloud_verification_status === Enums.CS_VERIFIED
					onTriggered: {
						globalDrawer.close()
						detailsWindow.endEditMode()
						manager.saveChangesCloud(true);
						globalDrawer.close()
					}
				}
				Kirigami.Action {
				icon {
					name: PrefCloudStorage.cloud_auto_sync ?  ":/icons/ic_cloud_off.svg" : ":/icons/ic_cloud_done.svg"
				}
				text: PrefCloudStorage.cloud_auto_sync ? qsTr("Disable auto cloud sync") : qsTr("Enable auto cloud sync")
					visible: Backend.cloud_verification_status !== Enums.CS_NOCLOUD
					onTriggered: {
						PrefCloudStorage.cloud_auto_sync = !PrefCloudStorage.cloud_auto_sync
						manager.setGitLocalOnly(PrefCloudStorage.cloud_auto_sync)
						if (!PrefCloudStorage.cloud_auto_sync) {
							showPassiveNotification(qsTr("Turning off automatic sync to cloud causes all data to only be \
stored locally. This can be very useful in situations with limited or no network access. Please choose 'Manual sync with cloud' \
if you have network connectivity and want to sync your data to cloud storage."), 10000)
						}
					}
				}
				Kirigami.Action {
					icon {
						name: ":/icons/sigma.svg"
					}
					text: qsTr("Dive summary")
					onTriggered: {
						globalDrawer.close()
						showPage(diveSummaryWindow)
						detailsWindow.endEditMode()
					}
				}
				Kirigami.Action {
					icon {
						name: ":/icons/ic_cloud_upload.svg"
					}
					text: qsTr("Export")
					onTriggered: {
						globalDrawer.close()
						showPage(exportWindow)
						detailsWindow.endEditMode()
					}
				}
			},
			Kirigami.Action {
				icon {
					name: ":/icons/map-globe.svg"
				}
				text: qsTr("Location")
				visible: true
				Kirigami.Action {
					icon {
						name: ":/go-previous-symbolic"
					}
					text: qsTr("Back")
					onTriggered: globalDrawer.scrollViewItem.pop()
				}
				Kirigami.Action {
					icon {
						name: ":/icons/map-globe.svg"
					}
					text: mapPage.title
					onTriggered: {
						showMap()
					}
				}
				Kirigami.Action {
					icon {
						name:":/icons/ic_gps_fixed.svg"
					}
					text: qsTr("Show GPS fixes")
					onTriggered: {
						globalDrawer.close()
						returnTopPage()
						manager.populateGpsData();
						showPage(gpsWindow)
					}
				}

				Kirigami.Action {
					icon {
						name: ":/icons/ic_clear.svg"
					}
					text: qsTr("Clear GPS cache")
					onTriggered: {
						globalDrawer.close();
						manager.clearGpsData();
					}
				}

				Kirigami.Action {
					icon {
						name: locationServiceEnabled ?  ":/icons/ic_location_off.svg" : ":/icons/ic_place.svg"
					}
					text: locationServiceEnabled ? qsTr("Disable location service") : qsTr("Run location service")
					onTriggered: {
						globalDrawer.close();
						locationServiceEnabled = !locationServiceEnabled
					}
				}
			},
			Kirigami.Action {
				icon {
					name: ":/icons/ic_settings.svg"
				}
				text: qsTr("Settings")
				onTriggered: {
					globalDrawer.close()
					settingsWindow.defaultCylinderModel = manager.cylinderInit
					PrefEquipment.default_cylinder === "" ? defaultCylinderIndex = "-1" : defaultCylinderIndex = settingsWindow.defaultCylinderModel.indexOf(PrefEquipment.default_cylinder)
					showPage(settingsWindow)
					detailsWindow.endEditMode()
				}
			},
			Kirigami.Action {
				icon {
					name: ":/icons/ic_help_outline.svg"
				}
				text: qsTr("Help")
				Kirigami.Action {
					icon {
						name: ":/go-previous-symbolic"
					}
					text: qsTr("Back")
					onTriggered: globalDrawer.scrollViewItem.pop()
				}
				Kirigami.Action {
					icon {
						name: ":/icons/ic_info_outline.svg"
					}
					text: qsTr("About")
					onTriggered: {
						globalDrawer.close()
						showPage(aboutWindow)
						detailsWindow.endEditMode()
					}
				}
				Kirigami.Action {
					icon {
						name: ":/icons/ic_help_outline.svg"
					}
					text: qsTr("Show user manual")
					onTriggered: {
						Qt.openUrlExternally("https://subsurface-divelog.org/documentation/subsurface-mobile-v3-user-manual/")
					}
				}
				Kirigami.Action {
					icon {
						name: ":/icons/contact_support.svg"
					}
					text: qsTr("Ask for support")
					onTriggered: {
						if (!manager.createSupportEmail()) {
							manager.copyAppLogToClipboard()
							showPassiveNotification(qsTr("failed to open email client, please manually create support email to support@subsurface-divelog.org - the logs have been copied to the clipboard and can be pasted into that email."), 6000)
						} else {
							globalDrawer.close()
						}
					}
				}
				Kirigami.Action{
					icon {
						name: ":/icons/account_circle.svg"
					}
					text: qsTr("Reset forgotten Subsurface Cloud password")
					onTriggered: {
						Qt.openUrlExternally("https://cloud.subsurface-divelog.org/passwordreset")
						globalDrawer.close()
					}
				}
			},
			Kirigami.Action {
				icon {
					name: ":/icons/ic_adb.svg"
				}
				text: qsTr("Developer")
				visible: PrefDisplay.show_developer
				Kirigami.Action {
					icon {
						name: ":/go-previous-symbolic"
					}
					text: qsTr("Back")
					onTriggered: globalDrawer.scrollViewItem.pop()
				}
				Kirigami.Action {
					text: qsTr("App log")
					onTriggered: {
						globalDrawer.close()
						showPage(logWindow)
					}
				}
				Kirigami.Action {
					text: qsTr("Test busy indicator (toggle)")
					onTriggered: {
						if (busy.running) {
							hideBusy()
						} else {
							showBusy()
						}
					}
				}
				Kirigami.Action {
					text: qsTr("Test notification text")
					onTriggered: {
						showPassiveNotification(qsTr("Test notification text"), 5000)
					}
				}
				Kirigami.Action {
					text: qsTr("Theme information")
					onTriggered: {
						globalDrawer.close()
						showPage(themetest)
					}
				}

				Kirigami.Action {
					text: qsTr("Enable verbose logging")
					onTriggered: {
						showPassiveNotification(qsTr("Not persistent"), 3000)
						globalDrawer.close()
						manager.verboseEnabled = true
					}
				}

				/* disable for now
				Kirigami.Action {
					text: qsTr("Dive planner")

					Kirigami.Action {
						icon {
							name: ":/go-previous-symbolic"
						}
						text: qsTr("Back")
						onTriggered: globalDrawer.scrollViewItem.pop()
					}
					Kirigami.Action {
						text: qsTr("Setup")
						onTriggered: {
							globalDrawer.close()
							pageStack.push(divePlannerSetupWindow)
						}
					}
					Kirigami.Action {
						text: qsTr("Edit")
						onTriggered: {
							globalDrawer.close()
							pageStack.push(divePlannerEditWindow)
						}
					}
					Kirigami.Action {
						text: qsTr("View")
						onTriggered: {
							globalDrawer.close()
							pageStack.push(divePlannerViewWindow)
						}
					}
					Kirigami.Action {
						text: qsTr("Manager")
						onTriggered: {
							globalDrawer.close()
							pageStack.push(divePlannerManagerWindow)
						}
					}
				}
				*/
			}
		] // end actions
		Image {
			fillMode: Image.PreserveAspectFit
			source: "qrc:///icons/" + (subsurfaceTheme.currentTheme !== "" ? subsurfaceTheme.currentTheme : "Blue") + "_gps.svg"
			visible: locationServiceEnabled
		}
	}

	function setupUnits() {
		// some screens are too narrow for Subsurface-mobile to render well
		// try to hack around that by making sure that we can fit at least 21 gridUnits in a row
		var numColumns = Math.max(Math.floor(rootItem.width / (21 * Kirigami.Units.gridUnit)), 1)
		if (Screen.primaryOrientation === Qt.PortraitOrientation && PrefDisplay.singleColumnPortrait) {
			manager.appendTextToLog("show only one column in portrait mode");
			numColumns = 1;
		}

		rootItem.colWidth = numColumns > 1 ? Math.floor(rootItem.width / numColumns) : rootItem.width;
		var kirigamiGridUnit = Kirigami.Units.gridUnit
		var widthInGridUnits = Math.floor(rootItem.colWidth / kirigamiGridUnit)
		if (widthInGridUnits < 21) {
			kirigamiGridUnit = Math.floor(rootItem.colWidth / 21)
			widthInGridUnits = Math.floor(rootItem.colWidth / kirigamiGridUnit)
		}
		var factor = 1.0
		manager.appendTextToLog(numColumns + " columns with column width of " + rootItem.colWidth)
		manager.appendTextToLog("width in Grid Units " + widthInGridUnits + " original gridUnit " + Kirigami.Units.gridUnit + " now " + kirigamiGridUnit)
		if (Kirigami.Units.gridUnit !== kirigamiGridUnit) {
			factor = kirigamiGridUnit / Kirigami.Units.gridUnit
			// change our glabal grid unit
			Kirigami.Units.gridUnit = kirigamiGridUnit
		}
		pageStack.defaultColumnWidth = rootItem.colWidth
		manager.appendTextToLog("Done setting up sizes")
	}

	QtObject {
		id: screenSizeObject

		property int initialWidth: rootItem.width
		property int initialHeight: rootItem.height
		property bool firstChange: true
		property int lastOrientation: undefined

		Component.onCompleted: {
			// break the binding
			initialWidth = initialWidth * 1
			manager.appendTextToLog("screenSizeObject constructor completed, initial width " + initialWidth)
			setupUnits()
		}
	}

	onWidthChanged: {
		manager.appendTextToLog("Window width changed to " + width + " orientation " + Screen.primaryOrientation)
		if (screenSizeObject.initialWidth !== undefined) {
			if (width !== screenSizeObject.initialWidth && screenSizeObject.firstChange) {
				screenSizeObject.firstChange = false
				screenSizeObject.lastOrientation = Screen.primaryOrientation
				screenSizeObject.initialWidth = width
				screenSizeObject.initialHeight = height
				manager.appendTextToLog("first real change, so recalculating units and recording size as " + width + " x " + height)
				setupUnits()
			} else if (screenSizeObject.lastOrientation !== undefined && screenSizeObject.lastOrientation !== Screen.primaryOrientation) {
				manager.appendTextToLog("Screen rotated, no action necessary")
				screenSizeObject.lastOrientation = Screen.primaryOrientation
				setupUnits()
			} else {
				manager.appendTextToLog("size change without rotation to " + width + " x " + height)
				if ((Qt.platform.os === "android" || Qt.platform.os === "ios") && width > screenSizeObject.initialWidth) {
					manager.appendTextToLog("resetting to initial width " + screenSizeObject.initialWidth + " and height " + screenSizeObject.initialHeight)
					rootItem.width = screenSizeObject.initialWidth
					rootItem.height = screenSizeObject.initialHeight
				}
			}
		} else {
			manager.appendTextToLog("width changed before initial width initialized, ignoring")
		}
	}

	property int hackToOpenMap: 0 /* Otherpage */
	/* I really want an enum, but those are painful in QML, so let's use numbers
	 * 0 (Otherpage)   - the last page selected was a non-map page
	 * 1 (MapSelected) - the map page was selected by the user
	 * 2 (MapForced)   - the map page was forced by this hack
	 */

	pageStack.onCurrentItemChanged: {
		// This is called whenever the user navigates using the breadcrumbs in the header

		if (pageStack.currentItem === null) {
			manager.appendTextToLog("there's no current page")
		} else {
			// horrible, insane hack to make picking the mapPage work
			// for some reason I cannot figure out, whenever the mapPage is selected
			// we immediately switch back to the page before it - so force-prevent
			// that undersired behavior
			if (pageStack.currentItem.objectName === mapPage.objectName) {
				// remember that we actively picked the mapPage
				if (hackToOpenMap !== 2 /* MapForced */ ) {
					manager.appendTextToLog("pageStack switched to map")
					hackToOpenMap = 1 /* MapSelected */
				} else {
					manager.appendTextToLog("pageStack forced back to map")
				}
			} else if (pageStack.currentItem.objectName !== mapPage.objectName &&
					   pageStack.lastItem.objectName === mapPage.objectName &&
					   hackToOpenMap === 1 /* MapSelected */) {
				// if we just picked the mapPage and are suddenly back on a different page
				// force things back to the mapPage
				manager.appendTextToLog("pageStack wrong page, switching back to map")
				pageStack.currentIndex = pageStack.contentItem.contentChildren.length - 1
				hackToOpenMap = 2 /* MapForced */
			} else {
				// if we picked a different page reset the mapPage hack
				manager.appendTextToLog("pageStack switched to " + pageStack.currentItem.objectName)
				hackToOpenMap = 0 /* Otherpage */
			}

			// disable the left swipe to go back when on the map page
			pageStack.interactive = pageStack.currentItem.objectName !== mapPage.objectName

			// is there a better way to reload the map markers instead of doing that
			// every time the map page is shown - e.g. link to the dive list model somehow?
			if (pageStack.currentItem.objectName === mapPage.objectName)
				mapPage.reloadMap()

			// In case we land on any page, not being the DiveDetails (which can be
			// in multiple states, such as add, edit or view), just end the edit/add mode
			if (pageStack.currentItem.objectName !== "DiveDetails" &&
					(detailsWindow.state === 'edit' || detailsWindow.state === 'add')) {
				detailsWindow.endEditMode()
			}
		}
	}

	QMLManager {
		id: manager
	}

	property bool initialized: manager.initialized

	onInitializedChanged: {
		if (initialized) {
			hideBusy()
			manager.appendTextToLog("initialization completed - showing the dive list")
			showPage(diveList) // we want to make sure that gets on the stack
			diveList.diveListModel = diveModel

			if (Qt.platform.os === "android") {
				manager.appendTextToLog("if we got started by a plugged in device, switch to download page -- pluggedInDeviceName = " + pluggedInDeviceName)
				if (pluggedInDeviceName !== "")
					// if we were started with a dive computer plugged in,
					// immediately switch to download page
					showDownloadForPluggedInDevice()
			}
		}
	}

	Label {
		id: textBlock
		visible: !initialized
		text: qsTr("Subsurface-mobile starting up")
		font.pointSize: subsurfaceTheme.headingPointSize
		topPadding: 2 * Kirigami.Units.gridUnit
		leftPadding: Kirigami.Units.gridUnit
	}

	StartPage {
		id: startPage
		anchors.fill: parent
		visible: initialized &&
			 Backend.cloud_verification_status !== Enums.CS_NOCLOUD &&
			 Backend.cloud_verification_status !== Enums.CS_VERIFIED
		onVisibleChanged: {
			manager.appendTextToLog("StartPage visibility changed to " + visible)
			if (!initialized) {
				manager.appendTextToLog("not yet initialized, show busy spinner")
				showBusy()
			}
			if (visible) {
				pageStack.clear()
			} else if (initialized) {
				showDiveList()
			}
		}
		Component.onCompleted: {
			if (Screen.manufacturer + " " + Screen.model + " " + Screen.name !== "  ")
				manager.appendTextToLog("Running on " + Screen.manufacturer + " " + Screen.model + " " + Screen.name)
			manager.appendTextToLog("StartPage completed -- initialized is " + initialized)
		}
	}

	DiveList {
		id: diveList
		visible: false
	}

	Settings {
		id: settingsWindow
		visible: false
	}

	CopySettings {
		id: settingsCopyWindow
		visible: false
	}

	About {
		id: aboutWindow
		visible: false
	}

	Export {
		id: exportWindow
		visible: false
	}

	DiveDetails {
		id: detailsWindow
		visible: false
	}

	TripDetails {
		id: tripEditWindow
		visible: false
	}

	Log {
		id: logWindow
		visible: false
	}

	GpsList {
		id: gpsWindow
		visible: false
	}

	DownloadFromDiveComputer {
		id: downloadFromDc
		visible: false
	}

	MapPage {
		id: mapPage
		visible: false
	}
/* this shouldn't be exposed unless someone will finish the work
	DivePlannerSetup {
		id: divePlannerSetupWindow
		visible: false
	}

	DivePlannerEdit {
		id: divePlannerEditWindow
		visible: false
	}

	DivePlannerView {
		id: divePlannerViewWindow
		visible: false
	}

	DivePlannerManager {
		id: divePlannerManagerWindow
		visible: false
	}
 */
	DiveSummary {
		id: diveSummaryWindow
		visible: false
	}

	ThemeTest {
		id: themetest
		visible: false
	}

	function showDownloadPage(vendor, product, connection) {
		manager.appendTextToLog("show download page for " + vendor + " / " + product + " / " + connection)
		downloadFromDc.dcImportModel.clearTable()
		if (vendor !== undefined && product !== undefined && connection !== undefined) {
			downloadFromDc.setupUSB = true
			// set up the correct values on the download page
			// setting the currentIndex to -1, first, helps to ensure
			// that the comboBox does get updated in the UI
			if (vendor !== -1) {
				downloadFromDc.vendor = -1
				downloadFromDc.vendor = vendor
			}
			if (product !== -1) {
				downloadFromDc.product = -1
				downloadFromDc.product = product
			}
			if (connection !== -1) {
				downloadFromDc.connection = -1
				downloadFromDc.connection = connection
			}
		} else {
			downloadFromDc.setupUSB = false
		}

		showPage(downloadFromDc)
	}

	function showDownloadForPluggedInDevice() {
		// don't add this unless the dive list is already shown
		if (pageIndex(diveList) === -1)
			return
		manager.appendTextToLog("plugged in device name changed to " + pluggedInDeviceName)
		/* if we recognized the device, we'll pass in a triple of ComboBox indeces as "vendor;product;connection" */
		var vendorProductConnection = pluggedInDeviceName.split(';')
		if (vendorProductConnection.length === 3)
			showDownloadPage(vendorProductConnection[0], vendorProductConnection[1], vendorProductConnection[2])
		else
			showDownloadPage()
	}

	onPluggedInDeviceNameChanged: {
		if (detailsWindow.state === 'edit' || detailsWindow.state === 'add') {
			/* we're in the middle of editing / adding a dive */
			manager.appendTextToLog("Download page requested by Android Intent, but adding/editing dive; no action taken")
		} else {
			// we want to show the downloads page
			// note that if Subsurface-mobile was started because a USB device was plugged in, this is run too early;
			// we catch this in the function below and instead switch to the download page in the completion signal
			// handler for the startPage
			showDownloadForPluggedInDevice()
		}
	}
	onClosing: {
		// this duplicates the check that is already in the onBackRequested signal handler of the DiveList
		if (globalDrawer.visible) {
			globalDrawer.close()
			close.accepted = false
		}
		if (contextDrawer.visible) {
			contextDrawer.close()
			close.accepted = false
		}
	}
}
