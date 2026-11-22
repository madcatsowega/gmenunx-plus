#include "powermanager.h"

PowerManager *PowerManager::instance = NULL;

PowerManager::PowerManager(GMenu2X *gmenu2x, uint32_t suspendTimeout, uint32_t powerTimeout):
gmenu2x(gmenu2x), suspendTimeout(suspendTimeout), powerTimeout(powerTimeout) {
	instance = this;

	resetSuspendTimer();
}

PowerManager::~PowerManager() {
	clearTimer();
	instance = NULL;
}

void PowerManager::setSuspendTimeout(uint32_t suspendTimeout) {
	this->suspendTimeout = suspendTimeout;
	resetSuspendTimer();
};

void PowerManager::setPowerTimeout(uint32_t powerTimeout) {
	this->powerTimeout = powerTimeout;
	resetSuspendTimer();
};

void PowerManager::clearTimer() {
	SDL_RemoveTimer(powerTimer); powerTimer = NULL;
};

void PowerManager::resetSuspendTimer() {
	clearTimer();
	if (this->suspendTimeout > 0)
		powerTimer = SDL_AddTimer(this->suspendTimeout * 1e3, doSuspend, NULL);
	else
		resetPowerTimer();
};

void PowerManager::resetPowerTimer() {
	clearTimer();
	if (this->powerTimeout > 0 && PowerManager::instance->gmenu2x->platform->opk != "linux")
		powerTimer = SDL_AddTimer(this->powerTimeout * 60e3, doPowerOff, NULL);
};

uint32_t PowerManager::doSuspend(uint32_t interval, void *param) {
	if (interval > 0) {
		// One-shot timer is finishing; clear handle before scheduling powerTimer
		PowerManager::instance->powerTimer = NULL;
		PowerManager::instance->suspendActive = true;
		// True PWM LED off (Kernel+ #12 handles boost-IC wake kick even at backlight=1)
		// and drop CPU safely to 240 MHz via /proc/jz/clock.
		PowerManager::instance->gmenu2x->setBacklight(0);
		PowerManager::instance->gmenu2x->platform->setCPU(240);
		PowerManager::instance->resetPowerTimer();
		InputManager::wakeUp(0, NULL); // ask main thread to paint black screen safely
		return 0;
	}

	PowerManager::instance->suspendActive = false;
	PowerManager::instance->gmenu2x->platform->setCPU(PowerManager::instance->gmenu2x->platform->cpu_menu);
	PowerManager::instance->gmenu2x->setBacklight(max(1, PowerManager::instance->gmenu2x->confInt["backlight"]));
	PowerManager::instance->resetSuspendTimer();
	return 0;
};

uint32_t PowerManager::doPowerOff(uint32_t interval, void *param) {
	system("sync; poweroff");
	return interval;
};
