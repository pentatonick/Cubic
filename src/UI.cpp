#include "UI.h"
#include "LocalPlayer.h"
#include "Game.h"
#include "Block.h"
#include "Resources.h"

#include <cstdio>
#include <ctime>
#include <string>
#include <filesystem>
#include <algorithm>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#if defined(EMSCRIPTEN)
#include <emscripten/html5.h>

EM_JS(void, copyToClipboard, (const char* text), {
	if (navigator.clipboard) navigator.clipboard.writeText(UTF8ToString(text));
});

EM_JS(void, toggleFullscreen, (), {
	if (!document.fullscreenElement && !document.mozFullScreenElement && !document.webkitFullscreenElement) {
         if (document.documentElement.requestFullscreen) {
			document.documentElement.requestFullscreen();
         } else if (document.documentElement.mozRequestFullScreen) {
			document.documentElement.mozRequestFullScreen();
         } else if (document.documentElement.webkitRequestFullscreen) {
			document.documentElement.webkitRequestFullscreen(Element.ALLOW_KEYBOARD_INPUT);
         }
	} else {
        if (document.cancelFullScreen) {
            document.cancelFullScreen();
        } else if (document.mozCancelFullScreen) {
            document.mozCancelFullScreen();
        } else if (document.webkitCancelFullScreen) {
			document.webkitCancelFullScreen();
        }
	}
});

EM_JS(bool, isTouchScreen, (), {
  return (('ontouchstart' in window) ||
	 (navigator.maxTouchPoints > 0) ||
	 (navigator.msMaxTouchPoints > 0));
});
#endif

void UI::init()
{
#if defined(EMSCRIPTEN)
	isTouch = isTouchScreen();
#elif defined(ANDROID) || TARGET_OS_IPHONE
	isTouch = true;
#else
    isTouch = false;
#endif

	state = State::None;
	touchState = (unsigned int)TouchState::None;

	mousePosition = glm::vec2();
	mouseState = MouseState::Up;

	blockVertices.init();

	fontVertices.init();
	fontTexture = game.textureManager.load(fontResourceTexture, sizeof(fontResourceTexture));

	interfaceVertices.init();
	interfaceTexture = game.textureManager.load(interfaceResourceTexture, sizeof(interfaceResourceTexture));
}

bool UI::input(const SDL_Event& event)
{
	if (isTouch)
	{
		if (event.type == SDL_FINGERMOTION)
		{
			mousePosition.x = event.tfinger.x * game.scaledWidth;
			mousePosition.y = event.tfinger.y * game.scaledHeight;

			for (auto touchPosition = touchPositions.begin(); touchPosition != touchPositions.end(); touchPosition++)
			{
				if (touchPosition->id == event.tfinger.fingerId)
				{
					if (glm::abs(event.tfinger.dx) > TOUCH_SWIPE_OFFSET || glm::abs(event.tfinger.dy) > TOUCH_SWIPE_OFFSET)
					{
						touchPosition->hold = false;
					}

					if (state == State::None && touchPosition->swipe)
					{
						game.localPlayer.turn(event.tfinger.dx * 360.0f, event.tfinger.dy * 180.0f);
					}

					touchPosition->x = event.tfinger.x * game.scaledWidth;
					touchPosition->y = event.tfinger.y * game.scaledHeight;

					break;
				}
			}

			const auto previousTouchState = touchState;

			if (drawTouchControls(true) || touchState != previousTouchState || state != UI::State::None)
			{
				update();
			}
		}
		else if (event.type == SDL_FINGERDOWN)
		{
			touchPositions.push_back({
				event.tfinger.fingerId,
				event.tfinger.x * game.scaledWidth,
				event.tfinger.y * game.scaledHeight,
				game.timer.milliTime(),
				true,
				true,
				false,
			});

			const auto previousTouchState = touchState;

			if (drawTouchControls(true) || touchState != previousTouchState || state != UI::State::None)
			{
				update();
			}
		}
		else if (event.type == SDL_FINGERUP)
		{
			for (auto touchPosition = touchPositions.begin(); touchPosition != touchPositions.end(); touchPosition++)
			{
				if (touchPosition->id == event.tfinger.fingerId)
				{
					if (touchPosition->isHolding)
					{
						game.localPlayer.interactState &= ~(unsigned int)LocalPlayer::Interact::Left;
					}
					else if (touchPosition->hold && game.timer.milliTime() - touchPosition->startTime <= TOUCH_PLACE_DELAY)
					{
						game.localPlayer.interactState |= (unsigned int)LocalPlayer::Interact::Right;
						game.localPlayer.interact();
						game.localPlayer.interactState &= ~(unsigned int)LocalPlayer::Interact::Right;
					}

					touchPositions.erase(touchPosition);
					break;
				}
			}

			mousePosition.x = event.tfinger.x * game.scaledWidth;
			mousePosition.y = event.tfinger.y * game.scaledHeight;
			mouseState = MouseState::Down;

			const auto previousTouchState = touchState;

			if (drawTouchControls(true) || touchState != previousTouchState || state != UI::State::None)
			{
				update();
			}

			mouseState = MouseState::Up;
		}

		return false;
	}
	else
	{
		if (event.type == SDL_KEYUP || event.type == SDL_CONTROLLERBUTTONUP)
		{
			if (event.key.keysym.sym == SDLK_ESCAPE || event.jbutton.button == SDL_CONTROLLER_BUTTON_START || event.jbutton.button == SDL_CONTROLLER_BUTTON_B)
			{
				if (state == State::None && event.jbutton.button != SDL_CONTROLLER_BUTTON_B)
				{
					openMainMenu();
					return false;
				}
				else if (state == State::SaveMenu || state == State::LoadMenu)
				{
					openMainMenu();
					return false;
				}
				else if (state == State::MainMenu)
				{
					closeMenu();
					return false;
				}
			}

			if (event.key.keysym.sym == SDLK_b || event.key.keysym.sym == SDLK_e || event.jbutton.button == SDL_CONTROLLER_BUTTON_Y || event.jbutton.button == SDL_CONTROLLER_BUTTON_B)
			{
				if (state == State::SelectBlockMenu)
				{
					closeMenu();
					return false;
				}
				else if (state == State::None && event.jbutton.button != SDL_CONTROLLER_BUTTON_B)
				{
					openMenu(State::SelectBlockMenu);
					return false;
				}
			}	
		}
		else if (event.type == SDL_CONTROLLERBUTTONDOWN)
		{
			if (state != State::None)
			{
				if (event.jbutton.button == SDL_CONTROLLER_BUTTON_A)
				{
					mouseState = MouseState::Down;

					update();

					mouseState = MouseState::Up;

					return false;
				}
				else if (
					event.jbutton.button == SDL_CONTROLLER_BUTTON_DPAD_LEFT ||
					event.jbutton.button == SDL_CONTROLLER_BUTTON_DPAD_RIGHT ||
					event.jbutton.button == SDL_CONTROLLER_BUTTON_DPAD_UP ||
					event.jbutton.button == SDL_CONTROLLER_BUTTON_DPAD_DOWN
				) 
				{
					glm::vec2* selectedButtonPosition{};

					for (auto& buttonPosition : buttonPositions)
					{
						if (buttonPosition.x == mousePosition.x && buttonPosition.y == mousePosition.y)
						{
							selectedButtonPosition = &buttonPosition;
							break;
						}
					}

					if (selectedButtonPosition)
					{
						glm::vec2* closestButtonPosition{};

						for (auto& buttonPosition : buttonPositions)
						{
							if (
								(event.jbutton.button == SDL_CONTROLLER_BUTTON_DPAD_RIGHT && buttonPosition.x > selectedButtonPosition->x) ||
								(event.jbutton.button == SDL_CONTROLLER_BUTTON_DPAD_LEFT && buttonPosition.x < selectedButtonPosition->x) ||
								(event.jbutton.button == SDL_CONTROLLER_BUTTON_DPAD_DOWN && buttonPosition.y > selectedButtonPosition->y) ||
								(event.jbutton.button == SDL_CONTROLLER_BUTTON_DPAD_UP && buttonPosition.y < selectedButtonPosition->y)
							)
							{
								if (
									!closestButtonPosition || 
									glm::distance(buttonPosition, *selectedButtonPosition) < glm::distance(*closestButtonPosition, *selectedButtonPosition)
								)
								{
									closestButtonPosition = &buttonPosition;
								}
							}
						}

						selectedButtonPosition = closestButtonPosition;
					}
					else if (!buttonPositions.empty())
					{
						selectedButtonPosition = &buttonPositions.front();
					}

					if (selectedButtonPosition)
					{
						mousePosition = { selectedButtonPosition->x, selectedButtonPosition->y };
						update();

						return false;
					}
				}
			}
		}
		else if (event.type == SDL_MOUSEMOTION)
		{
			if (state != State::None)
			{
				mousePosition.x = float(event.motion.x) / float(game.windowWidth) * game.scaledWidth;
				mousePosition.y = float(event.motion.y) / float(game.windowHeight) * game.scaledHeight;

				update();
				return false;
			}
		}
		else if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT)
		{
			if (state != State::None)
			{
				mousePosition.x = float(event.motion.x) / float(game.windowWidth) * game.scaledWidth;
				mousePosition.y = float(event.motion.y) / float(game.windowHeight) * game.scaledHeight;
				mouseState = MouseState::Down;
				
				update();
				return false;
			}
		}
		else if (event.type == SDL_MOUSEBUTTONUP && event.button.button == SDL_BUTTON_LEFT)
		{
			if (state != State::None)
			{
				mouseState = MouseState::Up;
				
				update();
				return false;
			}
		}
	}

	return true;
}

void UI::update()
{
	fontVertices.reset();
	interfaceVertices.reset();
	blockVertices.reset();

	if (state != State::None)
	{
		buttonPositions.clear();
	}

	if (state == State::StatusMenu)
	{
		if (drawStatusMenu())
		{
			return;
		}
	}
	else
	{
		drawHUD();

		if (state == State::SelectBlockMenu && drawSelectBlockMenu())
		{
			return;
		}

		if (isTouch && (state == State::None || state == State::SelectBlockMenu))
		{
			if (drawTouchControls())
			{
				return;
			}
		}

		if (state == State::MainMenu)
		{
			if (drawMainMenu())
			{
				return;
			}
		}
		else if (state == State::LoadMenu)
		{
			if (drawLoadMenu())
			{
				return;
			}
		}
		else if (state == State::SaveMenu)
		{
			if (drawSaveMenu())
			{
				return;
			}
		}
	}

	fontVertices.update();
	interfaceVertices.update();
	blockVertices.update();
}

void UI::render()
{
	blockVertices.render();

	glBindTexture(GL_TEXTURE_2D, fontTexture);
	fontVertices.render();

	glBindTexture(GL_TEXTURE_2D, interfaceTexture);
	interfaceVertices.render();
}

void UI::tick()
{
	if (!isTouch || state != State::None)
	{
		return;
	}

	for (auto& touchPosition : touchPositions)
	{
		if (touchPosition.hold && !touchPosition.isHolding && game.timer.milliTime() - touchPosition.startTime >= TOUCH_BREAK_DELAY)
		{
			game.localPlayer.interactState |= (unsigned int)LocalPlayer::Interact::Left;
			
			touchPosition.isHolding = true;
			break;
		}
	}
}

void UI::openMenu(UI::State newState, bool shouldUpdate)
{
	if (newState != State::None)
	{
		SDL_SetRelativeMouseMode(SDL_FALSE);

#if defined(EMSCRIPTEN)
		emscripten_exit_pointerlock();
#endif

		mousePosition = {};
		mouseState = MouseState::Up;
		state = newState;

		if (shouldUpdate)
		{
			update();
		}
	}
}

void UI::openStatusMenu(const char* title, const char* description, bool closeable)
{
	statusTitle = title;
	statusDescription = description;
	statusCloseable = closeable;

	openMenu(State::StatusMenu);
}

void UI::openMainMenu()
{
	mainMenuLastCopy = 0;

	openMenu(State::MainMenu);
}

void UI::closeMenu()
{
	SDL_SetRelativeMouseMode(SDL_TRUE);

	mouseState = MouseState::Up;
	state = State::None;

	update();
}

void UI::log(const char* format, ...)
{
	char buffer[4096];

	va_list args;
	va_start(args, format);
	int count = vsnprintf(buffer, sizeof(buffer), format, args);
	va_end(args);

	Log log;
	log.created = game.timer.milliTime();
	log.text = std::string(buffer, count);

	logs.push_back(log);

	update();
}

void UI::refresh()
{
	page = 0;
	saves.clear();

	for (const auto& entry : std::filesystem::directory_iterator(game.path))
	{
		auto path = entry.path();
		auto filename = path.filename().u8string();

		if (auto index = filename.find("Save "); index != std::string::npos)
		{
			auto lastWriteTime = entry.last_write_time();
			auto lastWriteSystemTime = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
				lastWriteTime - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now()
			);
			auto time = std::chrono::system_clock::to_time_t(lastWriteSystemTime);
			
			char timestamp[128];
			std::strftime(timestamp, sizeof(timestamp), " - %m/%d/%Y %H:%M:%S", std::localtime(&time));

			Save save;
			save.name = filename + timestamp;
			save.path = path.u8string();
			save.index = std::atoi(filename.erase(index, std::strlen("Save ")).c_str());
			saves.push_back(save);
		}
	}

	std::sort(saves.begin(), saves.end(), [](Save& save, Save& save2) { return save.index < save2.index; });
}

void UI::load(size_t index)
{
	if (index >= saves.size())
	{
		return;
	}

	FILE* file = fopen(saves[index].path.c_str(), "r");
	if (file)
	{
		fread(game.level.blocks.get(), Level::WIDTH * Level::HEIGHT * Level::DEPTH, sizeof(unsigned char), file);
		fclose(file);

		game.level.calculateLightDepths(0, 0, Level::WIDTH, Level::DEPTH);
		game.level.calculateSpawnPosition();
		game.level.reset();

		game.levelRenderer.loadAllChunks();
		game.network.sendLevel(UCHAR_MAX, true);
	}
}

void UI::save(size_t index)
{
	FILE* file;
	if (index < saves.size())
	{
		file = fopen(saves[index].path.c_str(), "w");
	}
	else
	{
		std::filesystem::path filename;
		filename /= game.path;
		filename /= std::string("Save ") + std::to_string(index + 1);

		file = fopen(filename.u8string().c_str(), "w");
	}

	if (file)
	{
		fwrite(game.level.blocks.get(), Level::WIDTH * Level::HEIGHT * Level::DEPTH, sizeof(unsigned char), file);
		fclose(file);

#if defined(EMSCRIPTEN)
		EM_ASM(
			FS.syncfs(false, function(err) {
				console.log(err);
			});
		);
#endif
	}
}

void UI::drawHUD()
{
	drawFPS();
	drawCrosshair();
	drawLogs();
	drawHotbar();
}

void UI::drawFPS()
{
	static std::string fps;
	fps.clear();

	fps += std::to_string(game.lastFrameRate);
	fps += " fps, ";
	fps += std::to_string(game.lastChunkUpdates);
	fps += " chunk updates";
	
	drawShadowedFont(fps.c_str(), 3.0f, 3.0f, 1.0f);
}

void UI::drawCrosshair()
{
	drawInterface(game.scaledWidth / 2 - 7, game.scaledHeight / 2 - 7, 211, 0, 16, 16);
}

void UI::drawLogs()
{
	for (auto log = logs.begin(); log != logs.end();)
	{
		auto index = logs.end() - log - 1;

		if (game.timer.milliTime() - log->created > 5000)
		{
			log = logs.erase(log);
		}
		else
		{
			float maxWidth = game.scaledWidth / 2 + 18.0f;
			float width = 0.0f;

			float interfaceY = game.scaledHeight - 35.0f - index * 10.0f;
			float fontY = game.scaledHeight - 33.8f - index * 10.0f;

			if (isTouch)
			{
				maxWidth = game.scaledWidth * 0.45f;

				auto offset = 14.0f;

				interfaceY = offset + index * 10.0f;
				fontY = offset + 1.2f + index * 10.0f;
			}

			for (size_t i = 0; i < log->text.length(); i++)
			{
				width += FONT_WIDTHS[int(log->text[i])];
			}

			if (width > maxWidth)
			{
				maxWidth = width + 4.0f;
			}

			drawInterface(0.0f, interfaceY, maxWidth, 10.0f, 183, 0, 16, 16, 0.12f);
			drawFont(log->text.c_str(), 1.8f, fontY, 1.0f, 1.1f);

			log++;
		}
	}
}

void UI::drawHotbar() 
{
	drawInterface(game.scaledWidth / 2 - 91 - (isTouch * 21 / 2), game.scaledHeight - 22, 0, 0, 182 + float(isTouch * 21), 22);

	if (isTouch)
	{
		drawInterface(game.scaledWidth / 2 - 91 + 179 - 21 / 2, game.scaledHeight - 22, 48, 23, 23, 22);
	}

	drawInterface(game.scaledWidth / 2 - 92 + float(game.localPlayer.inventoryIndex) * 20 - (isTouch * 21 / 2), game.scaledHeight - 23, 0, 22, 24, 24);

	for (int i = 0; i < LocalPlayer::INVENTORY_SIZE; i++)
	{
		auto blockType = game.localPlayer.inventory[i];

		float x = game.scaledWidth / 2.0f - 86.8f + i * 20.0f - (isTouch * 21 / 2);
		float y = game.scaledHeight - 6.8f;

		drawBlock(blockType, x, y, 9.8f);
	}
}

bool UI::drawTouchControls(bool invisible)
{
	float buttonOffsetX = 30.0f;
	float buttonOffsetY = 25.0f;
	float buttonOffsetZ = 65.0f;

	float buttonSize = std::max(game.scaledHeight, game.scaledWidth) * 0.06f;
	float jumpButtonSize = std::max(game.scaledHeight, game.scaledWidth) * 0.0625f;

	bool middleTouch = drawTouchButton((unsigned int)UI::Cancellable::Hold | (unsigned int)UI::Cancellable::Swipe, buttonOffsetX + buttonSize, game.scaledHeight - 2 * buttonSize - buttonOffsetY, buttonOffsetZ, "", buttonSize, buttonSize, true, invisible);
	if (middleTouch)
	{
		touchState |= (unsigned int)UI::TouchState::Middle;
	}
	else
	{
		touchState &= ~(unsigned int)UI::TouchState::Middle;
	}

	if (drawTouchButton((unsigned int)UI::Cancellable::Hold | (unsigned int)UI::Cancellable::Swipe, buttonOffsetX + buttonSize, game.scaledHeight - buttonSize - buttonOffsetY, buttonOffsetZ, "\x1F", buttonSize, buttonSize, true, invisible))
	{
		game.localPlayer.moveState |= (unsigned int)LocalPlayer::Move::Backward;

		touchState |= (unsigned int)UI::TouchState::Down;
	}
	else 
	{
		if (!middleTouch)
		{
			game.localPlayer.moveState &= ~(unsigned int)LocalPlayer::Move::Backward;
		}

		touchState &= ~(unsigned int)UI::TouchState::Down;
	}

	if (drawTouchButton((unsigned int)UI::Cancellable::Hold | (unsigned int)UI::Cancellable::Swipe, buttonOffsetX, game.scaledHeight - 2 * buttonSize - buttonOffsetY, buttonOffsetZ, "\x11", buttonSize, buttonSize, true, invisible))
	{
		game.localPlayer.moveState |= (unsigned int)LocalPlayer::Move::Left;

		touchState |= (unsigned int)UI::TouchState::Left;
	}
	else 
	{
		if (!middleTouch)
		{
			game.localPlayer.moveState &= ~(unsigned int)LocalPlayer::Move::Left;
		}

		touchState &= ~(unsigned int)UI::TouchState::Left;
	}

	if (drawTouchButton((unsigned int)UI::Cancellable::Hold | (unsigned int)UI::Cancellable::Swipe, buttonOffsetX + 2 * buttonSize, game.scaledHeight - 2 * buttonSize - buttonOffsetY, buttonOffsetZ, "\x10", buttonSize, buttonSize, true, invisible))
	{
		game.localPlayer.moveState |= (unsigned int)LocalPlayer::Move::Right;

		touchState |= (unsigned int)UI::TouchState::Right;
	}
	else 
	{
		if (!middleTouch)
		{
			game.localPlayer.moveState &= ~(unsigned int)LocalPlayer::Move::Right;
		}

		touchState &= ~(unsigned int)UI::TouchState::Right;
	}

	if (drawTouchButton((unsigned int)UI::Cancellable::Hold | (unsigned int)UI::Cancellable::Swipe, buttonOffsetX + buttonSize, game.scaledHeight - 3 * buttonSize - buttonOffsetY, buttonOffsetZ, "\x1E", buttonSize, buttonSize, true, invisible))
	{
		game.localPlayer.moveState |= (unsigned int)LocalPlayer::Move::Forward;

		touchState |= (unsigned int)UI::TouchState::Up;
	}
	else 
	{
		if (!middleTouch)
		{
			game.localPlayer.moveState &= ~(unsigned int)LocalPlayer::Move::Forward;
		}

		touchState &= ~(unsigned int)UI::TouchState::Up;
	}

	if (drawTouchButton((unsigned int)UI::Cancellable::Hold, game.scaledWidth - buttonOffsetX - 1.5f * buttonSize, game.scaledHeight - 2 * buttonSize - buttonOffsetY, buttonOffsetZ, game.localPlayer.noPhysics ? "\x7" : "\x4", jumpButtonSize, jumpButtonSize, true, invisible))
	{
		if (middleTouch && game.localPlayer.moveState == (unsigned int)LocalPlayer::Move::None)
		{
			game.localPlayer.noPhysics = !game.localPlayer.noPhysics;
		}

		game.localPlayer.moveState |= (unsigned int)LocalPlayer::Move::Jump;

		touchState |= (unsigned int)UI::TouchState::Jump;
	}
	else
	{
		game.localPlayer.moveState &= ~(unsigned int)LocalPlayer::Move::Jump;

		touchState &= ~(unsigned int)UI::TouchState::Jump;
	}

	float otherButtonOffsetX = 1.5f;
	float otherButtonOffsetY = 3.0f;
	float otherButtonSize = std::max(game.scaledHeight, game.scaledWidth) * 0.03f;

	if (drawTouchButton((unsigned int)UI::Cancellable::Hold, game.scaledWidth / 2 + otherButtonOffsetX, otherButtonOffsetY, buttonOffsetZ, "\xF0", otherButtonSize, otherButtonSize, invisible, invisible))
	{
		if (!invisible)
		{
			openMainMenu();

			return true;
		}
		else
		{
			touchState |= (unsigned int)UI::TouchState::Menu;
		}
	}
	else if (invisible)
	{
		touchState &= ~(unsigned int)UI::TouchState::Menu;
	}

	if (drawTouchButton((unsigned int)UI::Cancellable::Hold, game.scaledWidth / 2 - otherButtonSize - otherButtonOffsetX + 1.0f, otherButtonOffsetY, buttonOffsetZ, game.fullscreen ? "\x17" : "\x16", otherButtonSize, otherButtonSize, invisible, invisible))
	{
		if (!invisible)
		{
#if defined(EMSCRIPTEN)
			toggleFullscreen();
#else
			game.fullscreen = !game.fullscreen;

			SDL_SetWindowFullscreen(game.window, game.fullscreen);
#endif
		}
		else
		{
			touchState |= (unsigned int)UI::TouchState::Fullscreen;
		}
	}
	else if (invisible)
	{
		touchState &= ~(unsigned int)UI::TouchState::Fullscreen;
	}

	for (int i = 0; i < LocalPlayer::INVENTORY_SIZE + 1; i++)
	{
		bool touched = (state == State::None || state == State::SelectBlockMenu) && 
			drawTouchButton((unsigned int)UI::Cancellable::Hold | (unsigned int)UI::Cancellable::Swipe, game.scaledWidth / 2 - 90 + float(i) * 20 - 21 / 2, game.scaledHeight - 22 - 3.0f, buttonOffsetZ, "", 20, 22, false, true);

		if (i == LocalPlayer::INVENTORY_SIZE)
		{
			if (touched)
			{
				if (state == State::SelectBlockMenu)
				{
					closeMenu();
				}
				else
				{
					openMenu(State::SelectBlockMenu);
				}

				return true;
			}
		}
		else
		{
			if (touched)
			{
				game.localPlayer.inventoryIndex = i;
				game.heldBlock.update();

				mouseState = MouseState::Up;
				update();

				return true;
			}
		}
	}

	return false;
}

bool UI::drawStatusMenu()
{
	int x = int(glm::ceil(game.scaledWidth / 16));
	int y = int(glm::ceil(game.scaledHeight / 16));

	for (int i = 0; i < x; i++)
	{
		for (int j = 0; j < y; j++)
		{
			drawInterface(i * 16.0f, j * 16.0f, 240.0f, 0.0f, 16.0f, 16.0f, 0.25f, 0.9f);
		}
	}

	if (statusCloseable)
	{
#if defined(EMSCRIPTEN)
		drawCenteredFont(statusTitle.c_str(), game.scaledWidth / 2, game.scaledHeight / 2 - 25.0f, 0.6f);
		drawCenteredFont(statusDescription.c_str(), game.scaledWidth / 2, game.scaledHeight / 2 - 12.0f, 1.0f);

		if (drawButton(game.scaledWidth / 2 - 100, game.scaledHeight / 2 + 5.0f, 1.0f, game.network.isConnected() ? "Create a new room" : "Play offline"))
		{
			if (game.network.isConnected())
			{
				game.network.create();
			}
			else
			{
				closeMenu();
			}

			return true;
		}
#else
		if (game.network.isConnected())
		{
			float offset = -2.0f;

			drawCenteredFont(statusTitle.c_str(), game.scaledWidth / 2, game.scaledHeight / 2 - 38.0f + 2.0f + offset, 0.6f);
			drawCenteredFont(statusDescription.c_str(), game.scaledWidth / 2, game.scaledHeight / 2 - 25.0f + 2.0f + offset, 1.0f);

			if (drawButton(game.scaledWidth / 2 - 100, game.scaledHeight / 2 - 10.0f + 2.0f + offset, 1.0f, "Create a new room"))
			{
				if (game.network.isConnected())
				{
					game.network.create();
				}
				else
				{
					closeMenu();
				}

				return true;
			}

			if (drawButton(game.scaledWidth / 2 - 100, game.scaledHeight / 2 + 13.0f + 2.0f + offset, 1.0f, "Join room"))
			{
				char* clipboardText = SDL_GetClipboardText();
				size_t clipboardTextLength = strlen(clipboardText);
				size_t clipboardTextOffset = 0;

				for (size_t offset = 0; offset < clipboardTextLength; offset++)
				{
					if (clipboardText[offset] == '#')
					{
						clipboardTextOffset = offset + 1;
						break;
					}
				}

				game.network.join(std::string(clipboardText + clipboardTextOffset));

				SDL_free(clipboardText);
				return true;
			}
		}
		else
		{
			drawCenteredFont(statusTitle.c_str(), game.scaledWidth / 2, game.scaledHeight / 2 - 25.0f, 0.6f);
			drawCenteredFont(statusDescription.c_str(), game.scaledWidth / 2, game.scaledHeight / 2 - 12.0f, 1.0f);

			if (drawButton(game.scaledWidth / 2 - 100, game.scaledHeight / 2 + 5.0f, 1.0f, "Play offline"))
			{
				closeMenu();
				return true;
			}
		}
#endif
	}
	else
	{
		drawCenteredFont(statusTitle.c_str(), game.scaledWidth / 2, game.scaledHeight / 2 - 13.0f, 0.6f);
		drawCenteredFont(statusDescription.c_str(), game.scaledWidth / 2, game.scaledHeight / 2, 1.0f);
	}

	return false;
}

bool UI::drawLoadMenu()
{
	const float offset = 73.5f;

	drawInterface(0.0f, 0.0f, game.scaledWidth, game.scaledHeight, 183, 0, 16, 16, 0.08f, 64.0f);
	drawCenteredFont("Load Level", game.scaledWidth / 2, game.scaledHeight / 2 - offset, 1.0f, 65.0f);

	for (int i = 0; i < 4; i++)
	{
		if (drawButton(game.scaledWidth / 2 - 100, game.scaledHeight / 2 - offset + 16 + 24 * i, 65.0f, saves.size() >= i + 1 + 4 * page ? saves[i + 4 * page].name.c_str() : "-", saves.size() >= i + 1 + 4 * page))
		{
			load(i + 4 * page);

			closeMenu();
			return true;
		}
	}

	if (drawButton(game.scaledWidth / 2 - 130.0f, game.scaledHeight / 2 - offset + 16 + 24 + 24 - 10.0f, 65.0f, "<", page > 0, 20.0f))
	{
		page--;

		openMenu(State::LoadMenu);
		return true;
	}

	if (drawButton(game.scaledWidth / 2 + 110.0f, game.scaledHeight / 2 - offset + 16 + 24 + 24 - 10.0f, 65.0f, ">", page < (saves.size() / 4), 20.0f))
	{
		page++;

		openMenu(State::LoadMenu);
		return true;
	}

	if (drawButton(game.scaledWidth / 2 - 100, game.scaledHeight / 2 - offset + 16 + 24 + 24 + 24 + 36, 65.0f, "Back to Menu"))
	{
		openMainMenu();
		return true;
	}

	return false;
}

bool UI::drawSaveMenu()
{
	const float offset = 73.5f;

	drawInterface(0.0f, 0.0f, game.scaledWidth, game.scaledHeight, 183, 0, 16, 16, 0.08f, 64.0f);
	drawCenteredFont("Save Level", game.scaledWidth / 2, game.scaledHeight / 2 - offset, 1.0f, 65.0f);

	for (int i = 0; i < 4; i++)
	{
		if (drawButton(game.scaledWidth / 2 - 100, game.scaledHeight / 2 - offset + 16 + 24 * i, 65.0f, saves.size() >= i + 1 + 4 * page ? saves[i + 4 * page].name.c_str() : "-", saves.size() >= i + 4 * page))
		{
			save(i + 4 * page);

			openMainMenu();
			return true;
		}
	}

	if (drawButton(game.scaledWidth / 2 - 130.0f, game.scaledHeight / 2 - offset + 16 + 24 + 24 - 10.0f, 65.0f, "<", page > 0, 20.0f))
	{
		page--;

		openMenu(State::SaveMenu);
		return true;
	}

	if (drawButton(game.scaledWidth / 2 + 110.0f, game.scaledHeight / 2 - offset + 16 + 24 + 24 - 10.0f, 65.0f, ">", page < (saves.size() / 4), 20.0f))
	{
		page++;

		openMenu(State::SaveMenu);
		return true;
	}


	if (drawButton(game.scaledWidth / 2 - 100, game.scaledHeight / 2 - offset + 16 + 24 + 24 + 24 + 36, 65.0f, "Back to Menu"))
	{
		openMainMenu();
		return true;
	}

	return false;
}

bool UI::drawMainMenu()
{
	const float offset = 73.5f;
	const float optionsOffset = 80.0f;

	drawInterface(0.0f, 0.0f, game.scaledWidth, game.scaledHeight, 183, 0, 16, 16, 0.08f, 64.0f);
	drawCenteredFont("Main Menu", game.scaledWidth / 2, game.scaledHeight / 2 - offset, 1.0f, 65.0f);

	if (drawButton(game.scaledWidth / 2 - 100, game.scaledHeight / 2 - offset + 16, 65.0f, "Back to Game"))
	{
		closeMenu();
		return true;
	}

	if (drawButton(game.scaledWidth / 2 - 100, game.scaledHeight / 2 - offset + 40, 65.0f, "Load Level", game.network.isHost() || !game.network.isConnected(), 98.0f))
	{
		refresh();
		openMenu(State::LoadMenu);

		return true;
	}

	if (drawButton(game.scaledWidth / 2, game.scaledHeight / 2 - offset + 40, 65.0f, "Save Level", 1, 100.0f))
	{
		refresh();
		openMenu(State::SaveMenu);

		return true;
	}

	drawInterface(game.scaledWidth / 2 - 100, game.scaledHeight / 2 - offset + optionsOffset - 10.0f, 200.0f, 1.5f, 183, 0, 16, 16, 0.0f, 64.0f);

	drawCenteredFont("Invite your friends by sharing the link", game.scaledWidth / 2, game.scaledHeight / 2 - offset + optionsOffset, 1.0f, 65.0f);
	drawButton(game.scaledWidth / 2 - 100, game.scaledHeight / 2 - offset + optionsOffset + 16, 65.0f, game.network.url.c_str(), 0);

	if (drawButton(game.scaledWidth / 2 - 100, game.scaledHeight / 2 - offset + optionsOffset + 24 + 16, 65.0f, game.timer.milliTime() - mainMenuLastCopy < 1000 ? "Copied!" : "Copy"))
	{
#if defined(EMSCRIPTEN)
		copyToClipboard(game.network.url.c_str());
#else
		SDL_SetClipboardText(game.network.url.c_str());
#endif
		mainMenuLastCopy = game.timer.milliTime();

		mouseState = MouseState::Up;
		update();

		return true;
	}

	return false;
}

bool UI::drawSelectBlockMenu()
{
	float left = game.scaledWidth / 2.0f - 196.0f / 2.0f;
	float top = game.scaledHeight / 2.0f - 143.0f / 2.0f;

	drawInterface(left - 4, top - 3, 0, 106, 204, 149.9f, 1.0f, 2.0f);

	for (unsigned char blockType = 0, selectedBlockType = 0, index = 0; blockType < std::size(Block::Definitions); blockType++)
	{
		if (!game.level.isAirTile(blockType) && !game.level.isWaterTile(blockType) && !game.level.isLavaTile(blockType))
		{
			int col = index % 8;
			int row = index / 8;

			float x = 10.0f + left + 23.0f * col;
			float y = 23.5f + top + 21.0f * row;

			float width = 23.5f;
			float height = 23.0f;

			if (drawSelectBlockButton(blockType, selectedBlockType, x, y, width, height))
			{
				game.localPlayer.inventory[game.localPlayer.inventoryIndex] = blockType;
				game.heldBlock.update();

				closeMenu();
				return true;
			}

			index++;
		}
	}

	return false;
}

bool UI::drawSelectBlockButton(unsigned char blockType, unsigned char& selectedBlockType, float x, float y, float width, float height)
{
	bool clicked = mouseState == MouseState::Down;

	float hoverX = x - 4.5f;
	float hoverY = y - 16.0f;

	bool hover = mousePosition.x >= hoverX &&
		mousePosition.x <= hoverX + width &&
		mousePosition.y >= hoverY &&
		mousePosition.y <= hoverY + height;

	if (selectedBlockType == 0 && hover)
	{
		drawInterface(hoverX, hoverY, width, height, 183, 0, 16, 16, 0.7f, 2.0f);
		drawBlock(blockType, x - 1.2f, y + 1.0f, 12.0f);

		selectedBlockType = blockType;
	}
	else
	{
		drawBlock(blockType, x, y, 10.0f);
	}

	buttonPositions.push_back({ x, y });

	return hover && clicked;
}

bool UI::drawTouchButton(unsigned int flag, float x, float y, float z, const char* text, float width, float height, bool multiTouch, bool invisible)
{
	if (!invisible)
	{
		drawCenteredFont(text, x + width / 2, y + (height - 8) / 2, 1.0f, z + 100.0f);
	}
	
	for (auto touchPosition = touchPositions.begin(); touchPosition != touchPositions.end(); touchPosition++)
	{
		float hoverX = x;
		float hoverY = y;

		bool hover = touchPosition->x >= hoverX &&
			touchPosition->x <= hoverX + width &&
			touchPosition->y >= hoverY &&
			touchPosition->y <= hoverY + height;

		if (hover)
		{
			if (flag & (unsigned int)UI::Cancellable::Hold)
			{
				touchPosition->hold = false;
			}

			if (flag & (unsigned int)UI::Cancellable::Swipe)
			{
				touchPosition->swipe = false;
			}

			if (!invisible)
			{
				drawInterface(x, y, width, height, 27, 25, 18, 17, 0.7f, 64.0f);
			}

			if (multiTouch)
			{
				return true;
			}
		}
	}

	if (!multiTouch)
	{
		bool clicked = mouseState == MouseState::Down;

		float hoverX = x;
		float hoverY = y;

		bool hover = mousePosition.x >= hoverX &&
			mousePosition.x <= hoverX + width &&
			mousePosition.y >= hoverY &&
			mousePosition.y <= hoverY + height;

		if (hover && clicked)
		{
			if (!invisible)
			{
				drawInterface(x, y, width, height, 27, 25, 18, 17, 0.7f, 64.0f);
			}

			return true;
		}
	}

	if (!invisible)
	{
		drawInterface(x, y, width, height, 27, 25, 18, 17, 1.0f, 64.0f);
	}

	return false;
}

bool UI::drawButton(float x, float y, float z, const char* text, int state, float width, float height)
{
	bool clicked = mouseState == MouseState::Down;

	float hoverX = x;
	float hoverY = y;

	bool hover = mousePosition.x >= hoverX &&
		mousePosition.x <= hoverX + width &&
		mousePosition.y >= hoverY &&
		mousePosition.y <= hoverY + height;

	if (state && hover) { state = 2; }

	drawInterface(x, y, 0.0f, 46.0f + state * 19.99f, width / 2, height, 1.0f, z);
	drawInterface(x + width / 2, y, 200 - width / 2, 46.0f + state * 19.99f, width / 2, height, 1.0f, z);

	if (state)
	{
		drawCenteredFont(text, x + width / 2, y + (height - 8) / 2, 1.0f, z + 100.0f);

		buttonPositions.push_back({ x, y });
	}
	else
	{
		float size = 0.0f;
		size_t index = 0;

		const auto length = std::strlen(text);
		for (size_t i = 0; i < length; i++)
		{
			size += FONT_WIDTHS[int(text[i])];
			index = i;

			if (size > width - 15.0f)
			{
				break;
			}
		}

		auto truncatedText = std::string(text);
		if (length > 0 && index < length - 1)
		{
			truncatedText = truncatedText.substr(0, index) + "...";
		}

		drawCenteredFont(truncatedText.c_str(), x + width / 2, y + (height - 8) / 2, 0.7f, z + 100.0f);
	}

	return state && hover && clicked;
}

void UI::drawBlock(unsigned char blockType, float x, float y, float scale)
{
	auto blockDefinition = Block::Definitions[blockType];

	float u = 0.0625f * (blockDefinition.sideTexture % 16);
	float v = 0.0625f * (blockDefinition.sideTexture / 16) + (0.0625f - (0.0625f * blockDefinition.height));
	float u2 = 0.0625f + 0.0625f * (blockDefinition.sideTexture % 16);
	float v2 = 0.0625f + 0.0625f * (blockDefinition.sideTexture / 16);

	if (blockDefinition.draw == Block::DrawType::DRAW_SPRITE)
	{
		const VertexList::Vertex spriteVertexList[] = {
			{0.0f, 1.0f, 1.0f, u, v, 1.0f},
			{0.0f, 0.0f, 1.0f, u, v2, 1.0f},
			{1.0f, 0.0f, 1.0f, u2, v2, 1.0f},

			{0.0f, 1.0f, 1.0f, u, v, 1.0f},
			{1.0f, 0.0f, 1.0f, u2, v2, 1.0f},
			{1.0f, 1.0f, 1.0f, u2, v, 1.0f},
		};

		for (const auto& vertex : spriteVertexList)
		{
			glm::vec4 position = 
				glm::translate(glm::mat4(1.0f), glm::vec3(x + 2.0f, y + 1.0f, 15.0f)) * 
				glm::scale(glm::mat4(1.0f), glm::vec3(scale, -scale, scale)) *
				glm::vec4(vertex.x, vertex.y, vertex.z, 1.0f);

			blockVertices.push({position.x, position.y, position.z, vertex.u, vertex.v, vertex.s});
		}
	}
	else
	{
		float uTop = 0.0625f * (blockDefinition.topTexture % 16);
		float vTop = 0.0625f * (blockDefinition.topTexture / 16);
		float uTop2 = 0.0625f + 0.0625f * (blockDefinition.topTexture % 16);
		float vTop2 = 0.0625f + 0.0625f * (blockDefinition.topTexture / 16);

		const VertexList::Vertex blockVertexList[] = {
			{0.0f, blockDefinition.height, 0.0f, uTop, vTop, 1.0f},
			{0.0f, blockDefinition.height, 1.0f, uTop, vTop2, 1.0f},
			{1.0f, blockDefinition.height, 1.0f, uTop2, vTop2, 1.0f},

			{0.0f, blockDefinition.height, 0.0f, uTop, vTop, 1.0f},
			{1.0f, blockDefinition.height, 1.0f, uTop2, vTop2, 1.0f},
			{1.0f, blockDefinition.height, 0.0f, uTop2, vTop, 1.0f},

			{0.0f, blockDefinition.height, 1.0f, u, v, 0.8f},
			{0.0f, 0.0f, 1.0f, u, v2, 0.8f},
			{1.0f, 0.0f, 1.0f, u2, v2, 0.8f},

			{0.0f, blockDefinition.height, 1.0f, u, v, 0.8f},
			{1.0f, 0.0f, 1.0f, u2, v2, 0.8f},
			{1.0f, blockDefinition.height, 1.0f, u2, v, 0.8f},

			{0.0f, blockDefinition.height, 0.0f, u, v, 0.6f},
			{0.0f, 0.0f, 0.0f, u, v2, 0.6f},
			{0.0f, 0.0f, 1.0f, u2, v2, 0.6f},

			{0.0f, blockDefinition.height, 0.0f, u, v, 0.6f},
			{0.0f, 0.0f, 1.0f, u2, v2, 0.6f},
			{0.0f, blockDefinition.height, 1.0f, u2, v, 0.6f},
		};

		for (const auto& vertex : blockVertexList)
		{
			glm::vec4 position =
				glm::translate(glm::mat4(1.0f), glm::vec3(x, y, 15.0f)) * 
				glm::rotate(glm::mat4(1.0f), glm::radians(-30.0f), glm::vec3(1.0f, 0.0f, 0.0f)) *
				glm::rotate(glm::mat4(1.0f), glm::radians(45.0f), glm::vec3(0.0f, 1.0f, 0.0f)) *
				glm::scale(glm::mat4(1.0f), glm::vec3(scale, -scale, scale)) *
				glm::vec4(vertex.x, vertex.y, vertex.z, 1.0f);

			blockVertices.push({position.x, position.y, position.z, vertex.u, vertex.v, vertex.s});
		}
	}
}

void UI::drawInterface(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, float shade, float z)
{
	float size = 0.00390625f;

	interfaceVertices.push({x0, y0, z, u0 * size, v0 * size, shade});
	interfaceVertices.push({x0, y0 + y1, z, u0 * size, (v0 + v1) * size, shade});
	interfaceVertices.push({x0 + x1, y0 + y1, z, (u0 + u1) * size, (v0 + v1) * size, shade});

	interfaceVertices.push({x0, y0, z, u0 * size, v0 * size, shade});
	interfaceVertices.push({x0 + x1, y0 + y1, z, (u0 + u1) * size, (v0 + v1) * size, shade});
	interfaceVertices.push({x0 + x1, y0, z, (u0 + u1) * size, v0 * size, shade});
}

void UI::drawInterface(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, float shade)
{
	drawInterface(x0, y0, x1, y1, u0, v0, u1, v1, shade, 1.0f);
}

void UI::drawInterface(float x0, float y0, float x1, float y1, float u, float v, float shade, float z)
{
	float size = 0.00390625;

	interfaceVertices.push({x0, y0, z, x1 * size, y1 * size, shade});
	interfaceVertices.push({x0, y0 + v, z, x1 * size, (y1 + v) * size, shade});
	interfaceVertices.push({x0 + u, y0 + v, z, (x1 + u) * size, (y1 + v) * size, shade});

	interfaceVertices.push({x0, y0, z, x1 * size, y1 * size, shade});
	interfaceVertices.push({x0 + u, y0 + v, z, (x1 + u) * size, (y1 + v) * size, shade});
	interfaceVertices.push({x0 + u, y0, z, (x1 + u) * size, y1 * size, shade});
}

void UI::drawInterface(float x0, float y0, float x1, float y1, float u, float v, float shade)
{
	drawInterface(x0, y0, x1, y1, u, v, shade, 1.0f);
}

void UI::drawInterface(float x0, float y0, float x1, float y1, float u, float v)
{
	drawInterface(x0, y0, x1, y1, u, v, 1.0f);
}

void UI::drawFont(const char* text, float x, float y, float shade, float z)
{
	float width = 0.0f;

	const auto length = std::strlen(text);
	for (size_t index = 0; index < length; index++)
	{
		float u = float(text[index] % 16 << 3);
		float v = float(text[index] / 16 << 3);

		float height = 7.98f;

		fontVertices.push({x + width, y, z, u / 128.0f, v / 128.0f, shade});
		fontVertices.push({x + width, y + height, z, u / 128.0f, (v + height) / 128.0f, shade});
		fontVertices.push({x + width + height, y + height, z, (u + height) / 128.0f, (v + height) / 128.0f, shade});

		fontVertices.push({x + width, y, z, u / 128.0f, v / 128.0f, shade});
		fontVertices.push({x + width + height, y + height, z, (u + height) / 128.0f, (v + height) / 128.0f, shade});
		fontVertices.push({x + width + height, y, z, (u + height) / 128.0f, v / 128.0f, shade});

		width += FONT_WIDTHS[int(text[index])];
	}
}

void UI::drawShadowedFont(const char* text, float x, float y, float shade, float z)
{
	drawFont(text, x + 1.0f, y + 1.0f, 0.3f * shade, z);
	drawFont(text, x, y, shade, z);
}

void UI::drawShadowedFont(const char* text, float x, float y, float shade)
{
	drawShadowedFont(text, x, y, shade, 1.0f);
}

void UI::drawCenteredFont(const char* text, float x, float y, float shade, float z)
{
	float width = 0.0f;

	const auto length = std::strlen(text);
	for (size_t i = 0; i < length; i++)
	{
		width += FONT_WIDTHS[(unsigned char)text[i]];
	}

	drawShadowedFont(text, x - width / 2, y, shade, z);
}

void UI::drawCenteredFont(const char* text, float x, float y, float shade)
{
	drawCenteredFont(text, x, y, shade, 1.0f);
}
