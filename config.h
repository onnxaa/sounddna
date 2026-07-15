#define PLUG_NAME "SoundDNA"
#define PLUG_MFR "SoundDNA"
#define PLUG_VERSION_HEX 0x00010000
#define PLUG_VERSION_STR "1.0.0"
#define PLUG_UNIQUE_ID 'sdna'
#define PLUG_MFR_ID 'Sdna'
#define PLUG_URL_STR "https://sounddna.dev"
#define PLUG_EMAIL_STR "dev@sounddna.dev"
#define PLUG_COPYRIGHT_STR "Copyright 2025 SoundDNA"
#define PLUG_CLASS_NAME SoundDNA

#define BUNDLE_NAME "SoundDNA"
#define BUNDLE_MFR "SoundDNA"
#define BUNDLE_DOMAIN "dev"

#define BUNDLE_ID BUNDLE_DOMAIN "." BUNDLE_MFR "." BUNDLE_NAME API_EXT2
#define APP_GROUP_ID "group." BUNDLE_DOMAIN "." BUNDLE_MFR "." BUNDLE_NAME

#define SHARED_RESOURCES_SUBPATH "SoundDNA"

#define PLUG_CHANNEL_IO "2-2"

#define PLUG_LATENCY 0
#define PLUG_TYPE 0
#define PLUG_DOES_MIDI_IN 0
#define PLUG_DOES_MIDI_OUT 0
#define PLUG_DOES_MPE 0
#define PLUG_DOES_STATE_CHUNKS 0
#define PLUG_HAS_UI 1
#define PLUG_WIDTH 800
#define PLUG_HEIGHT 600
#define PLUG_FPS 60
#define PLUG_SHARED_RESOURCES 0
#define PLUG_HOST_RESIZE 1

#define AUV2_ENTRY SoundDNA_Entry
#define AUV2_ENTRY_STR "SoundDNA_Entry"
#define AUV2_FACTORY SoundDNA_Factory
#define AUV2_VIEW_CLASS SoundDNA_View
#define AUV2_VIEW_CLASS_STR "SoundDNA_View"

#define AAX_TYPE_IDS 'SDNA'
#define AAX_TYPE_IDS_AUDIOSUITE 'SDNA'
#define AAX_PLUG_MFR_STR "SoundDNA"
#define AAX_PLUG_NAME_STR "SoundDNA\nSdna"
#define AAX_PLUG_CATEGORY_STR "Effect"
#define AAX_DOES_AUDIOSUITE 1

#define VST3_SUBCATEGORY "Fx"

#define APP_NUM_CHANNELS 2
#define APP_N_VECTOR_WAIT 0
#define APP_MULT 1
#define APP_COPY_AUV3 0
#define APP_SIGNAL_VECTOR_SIZE 64
