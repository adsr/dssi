/* -*- c-basic-offset: 4 -*-  vi:set ts=8 sts=4 sw=4: */

/* trivial_sampler_qt_gui.cpp

   Disposable Hosted Soft Synth API
   Constructed by Chris Cannam, Steve Harris and Sean Bolton

   A straightforward DSSI plugin sampler: Qt GUI.

   This example file is in the public domain.
*/

#include "trivial_sampler_qt_gui.h"

#include <qapplication.h>
#include <qpushbutton.h>
#include <qtimer.h>
#include <qfiledialog.h>
#include <iostream>
#include <unistd.h>

#ifdef Q_WS_X11
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/SM/SMlib.h>

static int handle_x11_error(Display *dpy, XErrorEvent *err)
{
    char errstr[256];
    XGetErrorText(dpy, err->error_code, errstr, 256);
    if (err->error_code != BadWindow) {
	std::cerr << "trivial_sampler_qt_gui: X Error: "
		  << errstr << " " << err->error_code
		  << "\nin major opcode:  " << err->request_code << std::endl;
    }
    return 0;
}
#endif

using std::cerr;
using std::endl;

#define Sampler_PORT_BASE_PITCH    1
#define Sampler_PORT_SUSTAIN       2

lo_server osc_server = 0;

SamplerGUI::SamplerGUI(QString host, QString port,
		       QString controlPath, QString midiPath, QString configurePath,
		       QString exitingPath, QWidget *w) :
    QFrame(w),
    m_controlPath(controlPath),
    m_midiPath(midiPath),
    m_configurePath(configurePath),
    m_exitingPath(exitingPath),
    m_suppressHostUpdate(true),
    m_hostRequestedQuit(false),
    m_ready(false)
{
    m_host = lo_address_new(host, port);

    QGridLayout *layout = new QGridLayout(this, 2, 6, 5, 5);

    layout->addWidget(new QLabel("Sample file:  ", this), 0, 0, Qt::AlignRight);

    m_sampleFile = new QLabel("<none>", this);
    layout->addMultiCellWidget(m_sampleFile, 0, 0, 1, 4, Qt::AlignLeft);

    QPushButton *loadButton = new QPushButton("Open ...", this);
    layout->addWidget(loadButton, 0, 5, Qt::AlignLeft);
    connect(loadButton, SIGNAL(pressed()), this, SLOT(fileSelect()));

    layout->addWidget(new QLabel("Base pitch:", this), 1, 1, Qt::AlignRight);

    m_basePitch = new QSpinBox(this);
    m_basePitch->setMinValue(0);
    m_basePitch->setMaxValue(120);
    m_basePitch->setValue(60);
    layout->addWidget(m_basePitch, 1, 2, Qt::AlignLeft);
    connect(m_basePitch, SIGNAL(valueChanged(int)), this, SLOT(basePitchChanged(int)));
    
    layout->addWidget(new QLabel("  ", this), 1, 3, Qt::AlignRight);

    m_sustain = new QCheckBox("Sustain", this);
    m_sustain->setChecked(false);
    layout->addWidget(m_sustain, 1, 4, Qt::AlignRight);
    connect(m_sustain, SIGNAL(toggled(bool)), this, SLOT(sustainChanged(bool)));
    
    // cause some initial updates
    basePitchChanged (m_basePitch ->value());
    sustainChanged   (m_sustain   ->isChecked());

    QPushButton *testButton = new QPushButton("Test", this);
    connect(testButton, SIGNAL(pressed()), this, SLOT(test_press()));
    connect(testButton, SIGNAL(released()), this, SLOT(test_release()));
    layout->addWidget(testButton, 1, 5);

    QTimer *myTimer = new QTimer(this);
    connect(myTimer, SIGNAL(timeout()), this, SLOT(oscRecv()));
    myTimer->start(0, false);

    m_suppressHostUpdate = false;
}

void
SamplerGUI::setBasePitch(int pitch)
{
    m_suppressHostUpdate = true;
    m_basePitch->setValue(pitch);
    m_suppressHostUpdate = false;
}

void
SamplerGUI::setSustain(bool sustain)
{
    m_suppressHostUpdate = true;
    m_sustain->setChecked(sustain);
    m_suppressHostUpdate = false;
}

void
SamplerGUI::basePitchChanged(int value)
{
    if (!m_suppressHostUpdate) {
	lo_send(m_host, m_controlPath, "if", Sampler_PORT_BASE_PITCH, (float)value);
    }
}

void
SamplerGUI::sustainChanged(bool on)
{
    if (!m_suppressHostUpdate) {
	lo_send(m_host, m_controlPath, "if", Sampler_PORT_SUSTAIN, on ? 127.0 : 0.0);
    }
}

void
SamplerGUI::fileSelect()
{
    QString path = QFileDialog::getOpenFileName();
    if (path) {
	lo_send(m_host, m_configurePath, "ss", "load", path.latin1());
	m_sampleFile->setText(path);
    }
}

void
SamplerGUI::test_press()
{
    unsigned char noteon[4] = { 0x00, 0x90, 0x3C, 60 };

    lo_send(m_host, m_midiPath, "m", noteon);
}

void
SamplerGUI::oscRecv()
{
    if (osc_server) {
	lo_server_recv_noblock(osc_server, 1);
    }
}

void
SamplerGUI::test_release()
{
    unsigned char noteoff[4] = { 0x00, 0x90, 0x3C, 0x00 };

    lo_send(m_host, m_midiPath, "m", noteoff);
}

void
SamplerGUI::aboutToQuit()
{
    if (!m_hostRequestedQuit) lo_send(m_host, m_exitingPath, "");
}

SamplerGUI::~SamplerGUI()
{
    lo_address_free(m_host);
}


void
osc_error(int num, const char *msg, const char *path)
{
    cerr << "Error: liblo server error " << num
	 << " in path \"" << (path ? path : "(null)")
	 << "\": " << msg << endl;
}

int
debug_handler(const char *path, const char *types, lo_arg **argv,
	      int argc, void *data, void *user_data)
{
    int i;

    cerr << "Warning: unhandled OSC message in GUI:" << endl;

    for (i = 0; i < argc; ++i) {
	cerr << "arg " << i << ": type '" << types[i] << "': ";
        lo_arg_pp((lo_type)types[i], argv[i]);
	cerr << endl;
    }

    cerr << "(path is <" << path << ">)" << endl;
    return 1;
}

int
configure_handler(const char *path, const char *types, lo_arg **argv,
		  int argc, void *data, void *user_data)
{
    return 0;
}

int
show_handler(const char *path, const char *types, lo_arg **argv,
	     int argc, void *data, void *user_data)
{
    SamplerGUI *gui = static_cast<SamplerGUI *>(user_data);
    while (!gui->ready()) sleep(1);
    if (gui->isVisible()) gui->raise();
    else gui->show();
    return 0;
}

int
hide_handler(const char *path, const char *types, lo_arg **argv,
	     int argc, void *data, void *user_data)
{
    SamplerGUI *gui = static_cast<SamplerGUI *>(user_data);
    gui->hide();
    return 0;
}

int
quit_handler(const char *path, const char *types, lo_arg **argv,
	     int argc, void *data, void *user_data)
{
    SamplerGUI *gui = static_cast<SamplerGUI *>(user_data);
    gui->setHostRequestedQuit(true);
    qApp->quit();
    return 0;
}

int
control_handler(const char *path, const char *types, lo_arg **argv,
		int argc, void *data, void *user_data)
{
    SamplerGUI *gui = static_cast<SamplerGUI *>(user_data);

    if (argc < 2) {
	cerr << "Error: too few arguments to control_handler" << endl;
	return 1;
    }

    const int port = argv[0]->i;
    const float value = argv[1]->f;

    switch (port) {

    case Sampler_PORT_BASE_PITCH:
	gui->setBasePitch((int)value);
	break;

    case Sampler_PORT_SUSTAIN:
	gui->setSustain(value > 0.01 ? true : false);
	break;

    default:
	cerr << "Warning: received request to set nonexistent port " << port << endl;
    }

    return 0;
}

int
main(int argc, char **argv)
{
    cerr << "trivial_sampler_qt_gui starting..." << endl;

    QApplication application(argc, argv);

    if (application.argc() != 5) {
	cerr << "usage: "
	     << application.argv()[0] 
	     << " <osc url>"
	     << " <plugin dllname>"
	     << " <plugin label>"
	     << " <user-friendly id>"
	     << endl;
	return 2;
    }

#ifdef Q_WS_X11
    XSetErrorHandler(handle_x11_error);
#endif

    char *url = application.argv()[1];

    char *host = lo_url_get_hostname(url);
    char *port = lo_url_get_port(url);
    char *path = lo_url_get_path(url);

    SamplerGUI gui(host, port,
		 QString("%1/control").arg(path),
		 QString("%1/midi").arg(path),
		 QString("%1/configure").arg(path),
		 QString("%1/exiting").arg(path),
		 0);
		 
    application.setMainWidget(&gui);

    QString myControlPath = QString("%1/control").arg(path);
    QString myConfigurePath = QString("%1/configure").arg(path);
    QString myShowPath = QString("%1/show").arg(path);
    QString myHidePath = QString("%1/hide").arg(path);
    QString myQuitPath = QString("%1/quit").arg(path);

    osc_server = lo_server_new(NULL, osc_error);
    lo_server_add_method(osc_server, myControlPath, "if", control_handler, &gui);
    lo_server_add_method(osc_server, myConfigurePath, "ss", configure_handler, &gui);
    lo_server_add_method(osc_server, myShowPath, "", show_handler, &gui);
    lo_server_add_method(osc_server, myHidePath, "", hide_handler, &gui);
    lo_server_add_method(osc_server, myQuitPath, "", quit_handler, &gui);
    lo_server_add_method(osc_server, NULL, NULL, debug_handler, &gui);

    lo_address hostaddr = lo_address_new(host, port);
    lo_send(hostaddr,
	    QString("%1/update").arg(path),
	    "s",
	    QString("%1%2").arg(lo_server_get_url(osc_server)).arg(path+1).data());

    QObject::connect(&application, SIGNAL(aboutToQuit()), &gui, SLOT(aboutToQuit()));

    gui.setReady(true);
    return application.exec();
}

