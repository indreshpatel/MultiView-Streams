#include <obs-module.h>
#include <obs-frontend-api.h>
#include <media-io/video-io.h>
#include <util/config-file.h>
#include <util/platform.h>

#include <QDockWidget>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QMetaObject>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-dual-vertical-stream", "en-US")

MODULE_EXPORT const char *obs_module_description()
{
	return "Secondary vertical RTMP output for simultaneous 16:9 and 9:16 streaming.";
}

static constexpr const char *kConfigSection = "VerticalRTMP";
static constexpr const char *kOutputId = "rtmp_output";
static constexpr const char *kServiceId = "rtmp_custom";
static constexpr const char *kVideoEncoderId = "obs_x264";
static constexpr const char *kAudioEncoderId = "ffmpeg_aac";

class VerticalStreamDock;

static VerticalStreamDock *g_dock = nullptr;

struct CropRect {
	uint32_t x = 0;
	uint32_t y = 0;
	uint32_t width = 0;
	uint32_t height = 0;
};

static uint32_t even_floor(uint32_t value)
{
	return value & ~1u;
}

static CropRect make_center_crop_9x16(uint32_t source_width, uint32_t source_height)
{
	const double target = 9.0 / 16.0;
	const double current = static_cast<double>(source_width) / static_cast<double>(source_height);

	CropRect rect;

	if (current > target) {
		rect.height = source_height;
		rect.width = even_floor(static_cast<uint32_t>(source_height * target + 0.5));
		rect.x = even_floor((source_width - rect.width) / 2);
		rect.y = 0;
	} else {
		rect.width = source_width;
		rect.height = even_floor(static_cast<uint32_t>(source_width / target + 0.5));
		rect.x = 0;
		rect.y = even_floor((source_height - rect.height) / 2);
	}

	rect.width = std::max<uint32_t>(2, rect.width);
	rect.height = std::max<uint32_t>(2, rect.height);
	return rect;
}

class VerticalStreamDock final : public QDockWidget {
	Q_OBJECT

public:
	VerticalStreamDock(QWidget *parent = nullptr)
		: QDockWidget("Vertical RTMP", parent)
	{
		auto *root = new QWidget(this);
		auto *layout = new QVBoxLayout(root);

		auto *group = new QGroupBox("Vertical Stream", root);
		auto *form = new QFormLayout(group);

		urlEdit = new QLineEdit(group);
		keyEdit = new QLineEdit(group);
		keyEdit->setEchoMode(QLineEdit::Password);

		startButton = new QPushButton("Start Vertical Stream", group);
		stopButton = new QPushButton("Stop Vertical Stream", group);
		stopButton->setEnabled(false);

		statusLabel = new QLabel("Offline", group);

		form->addRow("Vertical RTMP URL", urlEdit);
		form->addRow("Vertical Stream Key", keyEdit);
		form->addRow("Status", statusLabel);
		form->addRow(startButton);
		form->addRow(stopButton);

		layout->addWidget(group);
		layout->addStretch(1);
		root->setLayout(layout);
		setWidget(root);

		loadConfig();

		connect(startButton, &QPushButton::clicked, this, &VerticalStreamDock::startVerticalStream);
		connect(stopButton, &QPushButton::clicked, this, &VerticalStreamDock::stopVerticalStream);
	}

	~VerticalStreamDock() override
	{
		stopVerticalStream();
		saveConfig();
	}

private slots:
	void startVerticalStream()
	{
		if (running.load())
			return;

		const std::string url = urlEdit->text().trimmed().toStdString();
		const std::string key = keyEdit->text().trimmed().toStdString();

		if (url.empty() || key.empty()) {
			setStatus("Offline: URL/key required");
			return;
		}

		saveConfig();

		obs_video_info ovi = {};
		if (!obs_get_video_info(&ovi)) {
			setStatus("Offline: OBS video unavailable");
			return;
		}

		sourceWidth = ovi.output_width;
		sourceHeight = ovi.output_height;
		crop = make_center_crop_9x16(sourceWidth, sourceHeight);

		video_output_info voi = {};
		voi.name = "vertical_rtmp_video";
		voi.format = VIDEO_FORMAT_BGRA;
		voi.fps_num = ovi.fps_num;
		voi.fps_den = ovi.fps_den;
		voi.width = crop.width;
		voi.height = crop.height;
		voi.cache_size = 6;
		voi.colorspace = VIDEO_CS_709;
		voi.range = VIDEO_RANGE_PARTIAL;

		if (video_output_open(&verticalVideo, &voi) != VIDEO_OUTPUT_SUCCESS) {
			setStatus("Offline: vertical video init failed");
			cleanupObsObjects();
			return;
		}

		video_scale_info vsi = {};
		vsi.format = VIDEO_FORMAT_BGRA;
		vsi.width = sourceWidth;
		vsi.height = sourceHeight;
		vsi.colorspace = VIDEO_CS_709;
		vsi.range = VIDEO_RANGE_PARTIAL;

		if (!video_output_connect(obs_get_video(), &vsi, &VerticalStreamDock::onMainVideoFrame, this)) {
			setStatus("Offline: main video hook failed");
			cleanupObsObjects();
			return;
		}

		videoConnected = true;

		if (!createObsOutput(url, key)) {
			setStatus("Offline: output creation failed");
			disconnectMainVideo();
			cleanupObsObjects();
			return;
		}

		connectOutputSignals();

		setStatus("Connecting");
		running.store(true);

		if (!obs_output_start(output)) {
			const char *lastError = obs_output_get_last_error(output);
			std::string msg = "Offline: start failed";
			if (lastError && *lastError) {
				msg += " - ";
				msg += lastError;
			}
			setStatus(QString::fromStdString(msg));
			running.store(false);
			disconnectMainVideo();
			cleanupObsObjects();
			updateButtons(false);
			return;
		}

		updateButtons(true);
	}

	void stopVerticalStream()
	{
		if (!running.load() && !output)
			return;

		setStatus("Stopping");

		if (output && obs_output_active(output)) {
			obs_output_stop(output);
		} else {
			running.store(false);
			disconnectMainVideo();
			cleanupObsObjects();
			updateButtons(false);
			setStatus("Offline");
		}
	}

private:
	QLineEdit *urlEdit = nullptr;
	QLineEdit *keyEdit = nullptr;
	QPushButton *startButton = nullptr;
	QPushButton *stopButton = nullptr;
	QLabel *statusLabel = nullptr;

	std::atomic_bool running{false};
	std::mutex frameMutex;

	obs_output_t *output = nullptr;
	obs_encoder_t *videoEncoder = nullptr;
	obs_encoder_t *audioEncoder = nullptr;
	obs_service_t *service = nullptr;
	video_t *verticalVideo = nullptr;

	bool videoConnected = false;
	uint32_t sourceWidth = 0;
	uint32_t sourceHeight = 0;
	CropRect crop;

	void loadConfig()
	{
		config_t *config = obs_frontend_get_profile_config();
		if (!config)
			return;

		const char *url = config_get_string(config, kConfigSection, "url");
		const char *key = config_get_string(config, kConfigSection, "key");

		if (url)
			urlEdit->setText(url);
		if (key)
			keyEdit->setText(key);
	}

	void saveConfig()
	{
		config_t *config = obs_frontend_get_profile_config();
		if (!config)
			return;

		config_set_string(config, kConfigSection, "url", urlEdit->text().toUtf8().constData());
		config_set_string(config, kConfigSection, "key", keyEdit->text().toUtf8().constData());
		config_save_safe(config, "tmp", nullptr);
	}

	bool createObsOutput(const std::string &url, const std::string &key)
	{
		obs_data_t *serviceSettings = obs_data_create();
		obs_data_set_string(serviceSettings, "server", url.c_str());
		obs_data_set_string(serviceSettings, "key", key.c_str());

		service = obs_service_create(kServiceId, "Vertical RTMP Service", serviceSettings, nullptr);
		obs_data_release(serviceSettings);

		obs_data_t *videoSettings = obs_data_create();
		obs_data_set_int(videoSettings, "bitrate", 4500);
		obs_data_set_int(videoSettings, "keyint_sec", 2);
		obs_data_set_string(videoSettings, "rate_control", "CBR");
		obs_data_set_string(videoSettings, "preset", "veryfast");

		videoEncoder = obs_video_encoder_create(kVideoEncoderId, "vertical_h264", videoSettings, nullptr);
		obs_data_release(videoSettings);

		obs_data_t *audioSettings = obs_data_create();
		obs_data_set_int(audioSettings, "bitrate", 160);

		audioEncoder = obs_audio_encoder_create(kAudioEncoderId, "vertical_aac", audioSettings, 0, nullptr);
		obs_data_release(audioSettings);

		obs_data_t *outputSettings = obs_data_create();
		output = obs_output_create(kOutputId, "vertical_rtmp_output", outputSettings, nullptr);
		obs_data_release(outputSettings);

		if (!service || !videoEncoder || !audioEncoder || !output || !verticalVideo)
			return false;

		obs_encoder_set_video(videoEncoder, verticalVideo);
		obs_encoder_set_audio(audioEncoder, obs_get_audio());

		obs_output_set_video_encoder(output, videoEncoder);
		obs_output_set_audio_encoder(output, audioEncoder, 0);
		obs_output_set_service(output, service);

		return true;
	}

	void connectOutputSignals()
	{
		if (!output)
			return;

		signal_handler_t *handler = obs_output_get_signal_handler(output);
		signal_handler_connect(handler, "start", &VerticalStreamDock::onOutputStart, this);
		signal_handler_connect(handler, "stop", &VerticalStreamDock::onOutputStop, this);
		signal_handler_connect(handler, "starting", &VerticalStreamDock::onOutputStarting, this);
	}

	void disconnectOutputSignals()
	{
		if (!output)
			return;

		signal_handler_t *handler = obs_output_get_signal_handler(output);
		signal_handler_disconnect(handler, "start", &VerticalStreamDock::onOutputStart, this);
		signal_handler_disconnect(handler, "stop", &VerticalStreamDock::onOutputStop, this);
		signal_handler_disconnect(handler, "starting", &VerticalStreamDock::onOutputStarting, this);
	}

	void disconnectMainVideo()
	{
		if (videoConnected) {
			video_output_disconnect(obs_get_video(), &VerticalStreamDock::onMainVideoFrame, this);
			videoConnected = false;
		}
	}

	void cleanupObsObjects()
	{
		std::lock_guard<std::mutex> lock(frameMutex);

		disconnectOutputSignals();

		if (output) {
			obs_output_release(output);
			output = nullptr;
		}

		if (videoEncoder) {
			obs_encoder_release(videoEncoder);
			videoEncoder = nullptr;
		}

		if (audioEncoder) {
			obs_encoder_release(audioEncoder);
			audioEncoder = nullptr;
		}

		if (service) {
			obs_service_release(service);
			service = nullptr;
		}

		if (verticalVideo) {
			video_output_close(verticalVideo);
			verticalVideo = nullptr;
		}
	}

	void setStatus(const QString &text)
	{
		statusLabel->setText(text);
	}

	void updateButtons(bool isRunning)
	{
		startButton->setEnabled(!isRunning);
		stopButton->setEnabled(isRunning);
	}

	static void onMainVideoFrame(void *param, video_data *frame)
	{
		auto *self = static_cast<VerticalStreamDock *>(param);
		self->copyCroppedFrame(frame);
	}

	void copyCroppedFrame(video_data *frame)
	{
		if (!running.load() || !frame || !frame->data[0])
			return;

		std::lock_guard<std::mutex> lock(frameMutex);

		if (!verticalVideo)
			return;

		video_frame out = {};
		if (!video_output_lock_frame(verticalVideo, &out, 1, frame->timestamp))
			return;

		const uint32_t bytesPerPixel = 4;
		const uint8_t *srcBase = frame->data[0] + (crop.y * frame->linesize[0]) + (crop.x * bytesPerPixel);
		uint8_t *dstBase = out.data[0];

		for (uint32_t y = 0; y < crop.height; ++y) {
			const uint8_t *src = srcBase + y * frame->linesize[0];
			uint8_t *dst = dstBase + y * out.linesize[0];
			std::memcpy(dst, src, crop.width * bytesPerPixel);
		}

		video_output_unlock_frame(verticalVideo);
	}

	static void onOutputStarting(void *param, calldata_t *)
	{
		auto *self = static_cast<VerticalStreamDock *>(param);
		QMetaObject::invokeMethod(self, [self]() { self->setStatus("Connecting"); }, Qt::QueuedConnection);
	}

	static void onOutputStart(void *param, calldata_t *)
	{
		auto *self = static_cast<VerticalStreamDock *>(param);
		QMetaObject::invokeMethod(self, [self]() { self->setStatus("Live"); }, Qt::QueuedConnection);
	}

	static void onOutputStop(void *param, calldata_t *data)
	{
		auto *self = static_cast<VerticalStreamDock *>(param);

		int code = (int)calldata_int(data, "code");
		const char *lastError = self->output ? obs_output_get_last_error(self->output) : nullptr;

		QMetaObject::invokeMethod(self, [self, code, err = std::string(lastError ? lastError : "")]() {
			self->running.store(false);
			self->disconnectMainVideo();
			self->cleanupObsObjects();
			self->updateButtons(false);

			if (code == OBS_OUTPUT_SUCCESS || code == OBS_OUTPUT_STOP)
				self->setStatus("Offline");
			else if (!err.empty())
				self->setStatus(QString("Offline: %1").arg(QString::fromStdString(err)));
			else
				self->setStatus(QString("Offline: error %1").arg(code));
		}, Qt::QueuedConnection);
	}
};

bool obs_module_load()
{
	g_dock = new VerticalStreamDock();

	if (!obs_frontend_add_dock_by_id("vertical_rtmp_dock", "Vertical RTMP", g_dock)) {
		delete g_dock;
		g_dock = nullptr;
		blog(LOG_ERROR, "[vertical-rtmp] Failed to add dock");
		return false;
	}

	blog(LOG_INFO, "[vertical-rtmp] Plugin loaded");
	return true;
}

void obs_module_unload()
{
	if (g_dock) {
		obs_frontend_remove_dock("vertical_rtmp_dock");
		delete g_dock;
		g_dock = nullptr;
	}

	blog(LOG_INFO, "[vertical-rtmp] Plugin unloaded");
}

#include "plugin.moc"