/* -*- c-basic-offset: 4 -*-  vi:set ts=8 sts=4 sw=4: */

/* less_trivial_synth_qt_gui.cpp

   Disposable Hosted Soft Synth API version 0.1
   Constructed by Chris Cannam and Steve Harris

   This is an example Qt GUI for an example DSSI synth plugin.

   This example file is in the public domain.
*/

#include "less_trivial_synth_qt_gui.h"
#include "osc_url.h"

#include <qapplication.h>
#include <iostream>

#define LTS_PORT_FREQ    1
#define LTS_PORT_ATTACK  2
#define LTS_PORT_DECAY   3
#define LTS_PORT_SUSTAIN 4
#define LTS_PORT_RELEASE 5

#define TEST_PATH "/dssi/test.1"

SynthGUI::SynthGUI(char *url, QWidget *w) :
    QFrame(w),
    m_suppressHostUpdate(true)
{
    char *host = osc_url_get_hostname(url);
    char *port = osc_url_get_port(url);
//    char *path = osc_url_get_path(url);

    m_host = lo_target_new(host, port);

    QGridLayout *layout = new QGridLayout(this, 3, 5, 5, 5);
    
    m_tuning  = new QDial(100, 600, 10, 400, this); // (Hz - 400) * 10
    m_attack  = new QDial(  1, 100,  1,  25, this); // s * 100
    m_decay   = new QDial(  1, 100,  1,  25, this); // s * 100
    m_sustain = new QDial(  0, 100,  1,  75, this); // %
    m_release = new QDial(  1, 400, 10, 200, this); // s * 100
    
    m_tuning ->setNotchesVisible(true);
    m_attack ->setNotchesVisible(true);
    m_decay  ->setNotchesVisible(true);
    m_sustain->setNotchesVisible(true);
    m_release->setNotchesVisible(true);

    m_tuningLabel  = new QLabel(this);
    m_attackLabel  = new QLabel(this);
    m_decayLabel   = new QLabel(this);
    m_sustainLabel = new QLabel(this);
    m_releaseLabel = new QLabel(this);
    
    layout->addWidget(new QLabel("Pitch of A", this), 0, 0, Qt::AlignCenter);
    layout->addWidget(new QLabel("Attack",     this), 0, 1, Qt::AlignCenter);
    layout->addWidget(new QLabel("Decay",      this), 0, 2, Qt::AlignCenter);
    layout->addWidget(new QLabel("Sustain",    this), 0, 3, Qt::AlignCenter);
    layout->addWidget(new QLabel("Release",    this), 0, 4, Qt::AlignCenter);
    
    layout->addWidget(m_tuning,  1, 0);
    layout->addWidget(m_attack,  1, 1);
    layout->addWidget(m_decay,   1, 2);
    layout->addWidget(m_sustain, 1, 3);
    layout->addWidget(m_release, 1, 4);
    
    layout->addWidget(m_tuningLabel,  2, 0, Qt::AlignCenter);
    layout->addWidget(m_attackLabel,  2, 1, Qt::AlignCenter);
    layout->addWidget(m_decayLabel,   2, 2, Qt::AlignCenter);
    layout->addWidget(m_sustainLabel, 2, 3, Qt::AlignCenter);
    layout->addWidget(m_releaseLabel, 2, 4, Qt::AlignCenter);

    connect(m_tuning,  SIGNAL(valueChanged(int)), this, SLOT(tuningChanged(int)));
    connect(m_attack,  SIGNAL(valueChanged(int)), this, SLOT(attackChanged(int)));
    connect(m_decay,   SIGNAL(valueChanged(int)), this, SLOT(decayChanged(int)));
    connect(m_sustain, SIGNAL(valueChanged(int)), this, SLOT(sustainChanged(int)));
    connect(m_release, SIGNAL(valueChanged(int)), this, SLOT(releaseChanged(int)));

    // cause some initial updates
    tuningChanged (m_tuning ->value());
    attackChanged (m_attack ->value());
    decayChanged  (m_decay  ->value());
    sustainChanged(m_sustain->value());
    releaseChanged(m_release->value());

    m_suppressHostUpdate = false;
}

void
SynthGUI::setTuning(float hz)
{
    m_suppressHostUpdate = true;
    m_tuning->setValue(int((hz - 400.0) / 10.0));
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
SynthGUI::tuningChanged(int value)
{
    float hz = float(value) / 10.0 + 400.0;
    m_tuningLabel->setText(QString("%1 Hz").arg(hz));

    if (!m_suppressHostUpdate) {
	std::cerr << "Sending to host: " << TEST_PATH
		  << " port " << LTS_PORT_FREQ << " to " << hz << std::endl;
	lo_send(m_host, TEST_PATH, "if", LTS_PORT_FREQ, hz);
    }
}

void
SynthGUI::attackChanged(int value)
{
    float sec = float(value) / 100.0;
    m_attackLabel->setText(QString("%1 sec").arg(sec));

    if (!m_suppressHostUpdate) {
	lo_send(m_host, TEST_PATH, "if", LTS_PORT_ATTACK, sec);
    }
}

void
SynthGUI::decayChanged(int value)
{
    float sec = float(value) / 100.0;
    m_decayLabel->setText(QString("%1 sec").arg(sec));

    if (!m_suppressHostUpdate) {
	lo_send(m_host, TEST_PATH, "if", LTS_PORT_DECAY, sec);
    }
}

void
SynthGUI::sustainChanged(int value)
{
    m_sustainLabel->setText(QString("%1 %").arg(value));

    if (!m_suppressHostUpdate) {
	lo_send(m_host, TEST_PATH, "if", LTS_PORT_SUSTAIN, float(value));
    }
}

void
SynthGUI::releaseChanged(int value)
{
    float sec = float(value) / 100.0;
    m_releaseLabel->setText(QString("%1 sec").arg(sec));

    if (!m_suppressHostUpdate) {
	lo_send(m_host, TEST_PATH, "if", LTS_PORT_RELEASE, sec);
    }
}

SynthGUI::~SynthGUI()
{
    lo_target_free(m_host);
}

int
main(int argc, char **argv)
{
    QApplication application(argc, argv);

    if (application.argc() != 2) {
	std::cerr << "usage: "
		  << application.argv()[0] 
		  << " <osc url>"
		  << std::endl;
	return 2;
    }

    SynthGUI gui(application.argv()[1]);
    application.setMainWidget(&gui);
    gui.show();

    //!!! now... updates from the host? calling the various setXXX
    // methods on the gui object when something is received.  (we could
    // make the gui object static for simplicity here.)

    return application.exec();
}

