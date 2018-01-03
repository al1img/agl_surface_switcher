/*
 *  AglLauncher class
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 * Copyright (C) 2016 EPAM Systems Inc.
 *
 */

#include "AglBeLauncher.hpp"

#include <core/dbus/asio/executor.h>

#include <unistd.h>

#include <xen/be/Exception.hpp>

#include "DBusItfControl.hpp"

using std::make_shared;
using std::this_thread::sleep_for;
using std::thread;
using std::chrono::milliseconds;

using XenBackend::Exception;

using namespace core::dbus;
using namespace com::epam;

/*******************************************************************************
 * AglBeLauncher
 ******************************************************************************/

AglBeLauncher::AglBeLauncher() :
	mSurfaceId(50),
	mLayerId(102 * 100000 + getpid()),
	mTerminate(false),
	mIsOnTop(false),
	mLog("AglBeLauncher")
{
	try
	{
		init();
	}
	catch(const std::exception& e)
	{
		release();

		throw;
	}
}

AglBeLauncher::~AglBeLauncher()
{
	release();
}

/*******************************************************************************
 * Private
 ******************************************************************************/

void AglBeLauncher::init()
{
	LOG(mLog, DEBUG) << "Init";

	LOG(mLog, DEBUG) << "Expected layer: " << mLayerId;

	auto ret = ilm_init();

	if (ret != ILM_SUCCESS)
	{
		throw Exception("Can't initialize ilm", ret);
	}

	mSurface.createSurface(mSurfaceId, 1080, 1487);

	mBus = make_shared<Bus>(WellKnownBus::session);
	mBus->install_executor(asio::make_executor(mBus));

	auto service = Service::use_service_or_throw_if_not_available(mBus, "com.epam.DeviceManager");

	mDBusThread = thread([this]() { mBus->run(); });

	mDBusObject = service->object_for_path(
			types::ObjectPath(DisplayManager::Control::default_path()));

	mPollThread = thread(&AglBeLauncher::run, this);
}

void AglBeLauncher::release()
{
	LOG(mLog, DEBUG) << "Release";

	mTerminate = true;

	if (mPollThread.joinable())
	{
		mPollThread.join();
	}

	if (mBus)
	{
		mBus->stop();
	}

	if (mDBusThread.joinable())
	{
		mDBusThread.join();
	}

//	ilm_destroy();
}
void AglBeLauncher::sendUserEvent(uint32_t event)
{
	Result<void> result =
			mDBusObject->transact_method<
				DisplayManager::Control::userEvent, void>(event);
}

void AglBeLauncher::run()
{
	while(!mTerminate)
	{
		try
		{
			sleep_for(milliseconds(100));

			ilmScreenProperties screenProperties;

			auto ret = ilm_getPropertiesOfScreen(0, &screenProperties);

			if (ret == ILM_SUCCESS)
			{
				if (screenProperties.layerCount)
				{
					if (screenProperties.layerIds[screenProperties.layerCount - 1] ==
						mLayerId && !mIsOnTop)
					{
						mIsOnTop = true;

						LOG(mLog, DEBUG) << "Show";

						sendUserEvent(1);
					}
					else if (screenProperties.layerIds[screenProperties.layerCount - 1] !=
							 mLayerId && mIsOnTop)
					{
						mIsOnTop = false;

						LOG(mLog, DEBUG) << "Hide";

						sendUserEvent(0);
					}
				}
				else
				{
					if (mIsOnTop)
					{
						LOG(mLog, DEBUG) << "Hide";

						sendUserEvent(0);
					}
				}

				free(screenProperties.layerIds);
			}
			else
			{
				throw Exception("Can't get screen properties", ret);
			}
		}
		catch(const std::exception& e)
		{
			LOG(mLog, ERROR) << e.what();
		}
	}
}
