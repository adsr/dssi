/* -*- c-basic-offset: 4 -*-  vi:set ts=8 sts=4 sw=4: */

/* less_trivial_synth_qt_gui.cpp

   Disposable Hosted Soft Synth API
   Constructed by Chris Cannam and Steve Harris

   This is an example Qt GUI for an example DSSI synth plugin.

   This example file is in the public domain.
*/

#include "less_trivial_synth_qt_gui.h"
#include "osc_url.h"

#include <qapplication.h>
#include <iostream>

using std::cerr;
using std::endl;

#define LTS_PORT_FREQ    1
#define LTS_PORT_ATTACK  2
#define LTS_PORT_DECAY   3
#define LTS_PORT_SUSTAIN 4
#define LTS_PORT_RELEASE 5
#define LTS_PORT_TIMBRE  6


SynthGUI::SynthGUI(char *host, char *port, char *path, QWidget *w) :
    QFrame(w),
    m_path(path),
    m_suppressHostUpdate(true)
{
    m_host = lo_address_new(host, port);

    QGridLayout *layout = new QGridLayout(this, 3, 5, 5, 5);
    
    m_tuning  = new QDial(100, 600, 10, 400, this); // (Hz - 400) * 10
    m_attack  = new QDial(  1, 100,  1,  25, this); // s * 100
    m_decay   = new QDial(  1, 100,  1,  25, this); // s * 100
    m_sustain = new QDial(  0, 100,  1,  75, this); // %
    m_release = new QDial(  1, 400, 10, 200, this); // s * 100
    m_timbre  = new QDial(  1, 100,  1,  25, this); // s * 100
    
    m_tuning ->setNotchesVisible(true);
    m_attack ->setNotchesVisible(true);
    m_decay  ->setNotchesVisible(true);
    m_sustain->setNotchesVisible(true);
    m_release->setNotchesVisible(true);
    m_timbre ->setNotchesVisible(true);

    m_tuningLabel  = new QLabel(this);
    m_attackLabel  = new QLabel(this);
    m_decayLabel   = new QLabel(this);
    m_sustainLabel = new QLabel(this);
    m_releaseLabel = new QLabel(this);
    m_timbreLabel  = new QLabel(this);
    
    layout->addWidget(new QLabel("Pitch of A", this), 0, 0, Qt::AlignCenter);
    layout->addWidget(new QLabel("Attack",     this), 0, 1, Qt::AlignCenter);
    layout->addWidget(new QLabel("Decay",      this), 0, 2, Qt::AlignCenter);
    layout->addWidget(new QLabel("Sustain",    this), 0, 3, Qt::AlignCenter);
    layout->addWidget(new QLabel("Release",    this), 0, 4, Qt::AlignCenter);
    layout->addWidget(new QLabel("Timbre",     this), 0, 5, Qt::AlignCenter);
    
    layout->addWidget(m_tuning,  1, 0);
    layout->addWidget(m_attack,  1, 1);
    layout->addWidget(m_decay,   1, 2);
    layout->addWidget(m_sustain, 1, 3);
    layout->addWidget(m_release, 1, 4);
    layout->addWidget(m_timbre,  1, 5);
    
    layout->addWidget(m_tuningLabel,  2, 0, Qt::AlignCenter);
    layout->addWidget(m_attackLabel,  2, 1, Qt::AlignCenter);
    layout->addWidget(m_decayLabel,   2, 2, Qt::AlignCenter);
    layout->addWidget(m_sustainLabel, 2, 3, Qt::AlignCenter);
    layout->addWidget(m_releaseLabel, 2, 4, Qt::AlignCenter);
    layout->addWidget(m_timbreLabel,  2, 5, Qt::AlignCenter);

    connect(m_tuning,  SIGNAL(valueChanged(int)), this, SLOT(tuningChanged(int)));
    connect(m_attack,  SIGNAL(valueChanged(int)), this, SLOT(attackChanged(int)));
    connect(m_decay,   SIGNAL(valueChanged(int)), this, SLOT(decayChanged(int)));
    connect(m_sustain, SIGNAL(valueChanged(int)), this, SLOT(sustainChanged(int)));
    connect(m_release, SIGNAL(valueChanged(int)), this, SLOT(releaseChanged(int)));
    connect(m_timbre,  SIGNAL(valueChanged(int)), this, SLOT(timbreChanged(int)));

    // cause some initial updates
    tuningChanged (m_tuning ->value());
    attackChanged (m_attack ->value());
    decayChanged  (m_decay  ->value());
    sustainChanged(m_sustain->value());
    releaseChanged(m_release->value());
    timbreChanged (m_timbre ->value());

    m_suppressHostUpdate = false;
}

void
SynthGUI::setTuning(float hz)
{
    m_suppressHostUpdate = true;
    m_tuning->setValue(int((hz - 400.0) * 10.0));
    m_suppressHostUpdate = false;
}

void
SynthGUI::setAttack(float sec)
{
    m_suppressHostUpdate = true;
    m_attack->setValue(int(sec * 100));
    m_suppressHostUpdate = false;
}

void
SynthGUI::setDecay(float sec)
{
    m_suppressHostUpdate = true;
    m_decay->setValue(int(sec * 100));
    m_suppressHostUpdate = false;
}

void
SynthGUI::setSustain(float percent)
{
    m_suppressHostUpdate = true;
    m_sustain->setValue(int(percent));
    m_suppressHostUpdate = false;
}

void
SynthGUI::setRelease(float sec)
{
    m_suppressHostUpdate = true;
    m_release->setValue(int(sec * 100));
    m_suppressHostUpdate = false;
}

void
SynthGUI::setTimbre(float val)
{
    m_suppressHostUpdate = true;
    m_timbre->setValue(int(val * 100));
    m_suppressHostUpdate = false;
}

void
SynthGUI::tuningChanged(int value)
{
    float hz = float(value) / 10.0 + 400.0;
    m_tuningLabel->setText(QString("%1 Hz").arg(hz));

    if (!m_suppressHostUpdate) {
	cerr << "Sending to host: " << m_path
	     << " port " << LTS_PORT_FREQ << " to " << hz << endl;
	lo_send(m_host, m_path, "if", LTS_PORT_FREQ, hz);
    }
}

void
SynthGUI::attackChanged(int value)
{
    float sec = float(value) / 100.0;
    m_attackLabel->setText(QString("%1 sec").arg(sec));

    if (!m_suppressHostUpdate) {
	lo_send(m_host, m_path, "if", LTS_PORT_ATTACK, sec);
    }
}

void
SynthGUI::decayChanged(int value)
{
    float sec = float(value) / 100.0;
    m_decayLabel->setText(QString("%1 sec").arg(sec));

    if (!m_suppressHostUpdate) {
	lo_send(m_host, m_path, "if", LTS_PORT_DECAY, sec);
    }
}

void
SynthGUI::sustainChanged(int value)
{
    m_sustainLabel->setText(QString("%1 %").arg(value));

    if (!m_suppressHostUpdate) {
	lo_send(m_host, m_path, "if", LTS_PORT_SUSTAIN, float(value));
    }
}

void
SynthGUI::releaseChanged(int value)
{
    float sec = float(value) / 100.0;
    m_releaseLabel->setText(QString("%1 sec").arg(sec));

    if (!m_suppressHostUpdate) {
	lo_send(m_host, m_path, "if", LTS_PORT_RELEASE, sec);
    }
}

void
SynthGUI::timbreChanged(int value)
{
    float val = float(value) / 100.0;
    m_releaseLabel->setText(QString("%1").arg(val));

    if (!m_suppressHostUpdate) {
	lo_send(m_host, m_path, "if", LTS_PORT_TIMBRE, val);
    }
}

SynthGUI::~SynthGUI()
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

    cerr << "Warning: unhandled OSC message:" << endl;

    for (i = 0; i < argc; ++i) {
	cerr << "arg " << i << ": type '" << types[i] << "': ";
        lo_arg_pp(types[i], argv[i]);
	cerr << endl;
    }

    cerr << "(path is <" << path << ">)" << endl;
    return 1;
}

int
update_handler(const char *path, const char *types, lo_arg **argv,
	       int argc, void *data, void *user_data)
{
    SynthGUI *gui = static_cast<SynthGUI *>(user_data);

    if (argc < 2) {
	cerr << "Error: too few arguments to update_handler" << endl;
	return 1;
    }

    const int port = argv[0]->i;
    const float value = argv[1]->f;

    switch (port) {

    case LTS_PORT_FREQ:
	cerr << "gui setting frequency to " << value << endl;
	gui->setTuning(value);
	break;

    case LTS_PORT_ATTACK:
	cerr << "gui setting attack to " << value << endl;
	gui->setAttack(value);
	break;

    case LTS_PORT_DECAY:
	cerr << "gui setting decay to " << value << endl;
	gui->setDecay(value);
	break;

    case LTS_PORT_SUSTAIN:
	cerr << "gui setting sustain to " << value << endl;
	gui->setSustain(value);
	break;

    case LTS_PORT_RELEASE:
	cerr << "gui setting release to " << value << endl;
	gui->setRelease(value);
	break;

    case LTS_PORT_TIMBRE:
	cerr << "gui setting timbre to " << value << endl;
	gui->setTimbre(value);
	break;

    default:
	cerr << "Warning: received request to set nonexistent port " << port << endl;
    }

    return 0;
}


int
main(int argc, char **argv)
{
    QApplication application(argc, argv);

    if (application.argc() != 2) {
	cerr << "usage: "
	     << application.argv()[0] 
	     << " <osc url>"
	     << endl;
	return 2;
    }

    char *url = application.argv()[1];

    char *host = osc_url_get_hostname(url);
    char *port = osc_url_get_port(url);
    char *path = osc_url_get_path(url);

    SynthGUI gui(host, port, path);
    application.setMainWidget(&gui);
    gui.show();

    lo_server_thread thread = lo_server_thread_new("4445", osc_error);
    lo_server_thread_add_method(thread, path, "if", update_handler, &gui);
    lo_server_thread_add_method(thread, NULL, NULL, debug_handler, &gui);
    lo_server_thread_start(thread);

    lo_address hostaddr = lo_address_new(host, port);
    lo_send(hostaddr, QString("%1/update").arg(path),
	    "s", "osc://localhost:4445/");

    return application.exec();
}

