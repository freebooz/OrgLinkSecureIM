import { Room, RoomEvent, Track } from 'livekit-client';

const participants = document.querySelector('#participants');
const status = document.querySelector('#status');
const roomName = document.querySelector('#room-name');
const microphone = document.querySelector('#microphone');
const camera = document.querySelector('#camera');
const screen = document.querySelector('#screen');
const audioUnlock = document.querySelector('#audio-unlock');
const leave = document.querySelector('#leave');

// JWT 只从 fragment 读取，随后立即清除地址栏，避免令牌进入代理日志、浏览器历史或复制链接。
const parameters = new URLSearchParams(location.hash.slice(1));
const serverUrl = parameters.get('server');
const participantToken = parameters.get('token');
const initialVideo = parameters.get('video') === '1';
history.replaceState(null, '', location.pathname);

const room = new Room({ adaptiveStream: true, dynacast: true });
const tiles = new Map();
let mediaReady = false;

/** 将浏览器媒体错误转换为用户可执行的中文提示，避免只显示底层英文异常。 */
function mediaErrorMessage(error) {
  const message = error instanceof Error ? error.message : '';
  const normalized = message.toLowerCase();
  if (normalized.includes('permission') || normalized.includes('notallowed'))
    return '麦克风或摄像头权限被拒绝，请在系统隐私设置中允许安域通访问。';
  if (normalized.includes('notfound') || normalized.includes('not found'))
    return '未找到可用的麦克风或摄像头，请检查设备连接。';
  if (normalized.includes('in use') || normalized.includes('deviceinuse'))
    return '媒体设备正在被其他程序占用，请关闭其他通话软件后重试。';
  return message || '媒体设备操作失败，请检查设备和权限。';
}

/** 根据远端音频播放权限显示解锁按钮；浏览器要求 startAudio 必须由用户手势触发。 */
function refreshAudioPlayback() {
  // LiveKit 在连接后会明确报告 canPlaybackAudio；不依赖内部连接状态字符串，避免版本差异导致按钮不显示。
  const blocked = room.canPlaybackAudio === false;
  audioUnlock.hidden = !blocked;
  if (blocked) status.textContent = '已连接，点击“启用远端音频”开始听取对方声音';
}

function participantKey(participant) {
  return participant.identity || 'local';
}

function ensureTile(participant) {
  const key = participantKey(participant);
  if (tiles.has(key)) return tiles.get(key);
  const tile = document.createElement('section');
  tile.className = 'participant';
  tile.dataset.identity = key;
  const name = document.createElement('div');
  name.className = 'name';
  name.textContent = participant.name || participant.identity || '当前用户';
  tile.append(name);
  participants.append(tile);
  tiles.set(key, tile);
  return tile;
}

function attachTrack(track, publication, participant) {
  const tile = ensureTile(participant);
  const element = track.attach();
  element.autoplay = true;
  if (track.kind === Track.Kind.Video) element.playsInline = true;
  element.dataset.trackSid = publication?.trackSid || track.sid || '';
  tile.prepend(element);
  // 远端音频轨道可能在连接后才到达；立即刷新状态，确保自动播放被浏览器拦截时能显示解锁按钮。
  if (track.kind === Track.Kind.Audio) refreshAudioPlayback();
}

function detachTrack(track) {
  track.detach().forEach((element) => element.remove());
}

function refreshControls() {
  microphone.classList.toggle('active', room.localParticipant.isMicrophoneEnabled);
  camera.classList.toggle('active', room.localParticipant.isCameraEnabled);
  screen.classList.toggle('active', room.localParticipant.isScreenShareEnabled);
}

room
  .on(RoomEvent.TrackSubscribed, attachTrack)
  .on(RoomEvent.TrackUnsubscribed, detachTrack)
  .on(RoomEvent.ParticipantConnected, ensureTile)
  .on(RoomEvent.ParticipantDisconnected, (participant) => {
    const key = participantKey(participant);
    tiles.get(key)?.remove();
    tiles.delete(key);
  })
  .on(RoomEvent.LocalTrackPublished, (publication) => {
    if (publication.track) attachTrack(publication.track, publication, room.localParticipant);
    refreshControls();
  })
  .on(RoomEvent.LocalTrackUnpublished, (publication) => {
    if (publication.track) detachTrack(publication.track);
    refreshControls();
  })
  .on(RoomEvent.Disconnected, () => {
    status.textContent = '已离开会议';
    mediaReady = false;
    audioUnlock.hidden = true;
  })
  .on(RoomEvent.MediaDevicesError, (error) => {
    status.textContent = mediaErrorMessage(error);
  })
  .on(RoomEvent.AudioPlaybackStatusChanged, refreshAudioPlayback);

async function runMediaAction(action) {
  if (!mediaReady) return;
  try {
    await action();
    refreshControls();
  } catch (error) {
    status.textContent = mediaErrorMessage(error);
  }
}

microphone.addEventListener('click', () => runMediaAction(async () => {
  await room.localParticipant.setMicrophoneEnabled(!room.localParticipant.isMicrophoneEnabled);
}));
camera.addEventListener('click', () => runMediaAction(async () => {
  await room.localParticipant.setCameraEnabled(!room.localParticipant.isCameraEnabled);
}));
screen.addEventListener('click', () => runMediaAction(async () => {
  await room.localParticipant.setScreenShareEnabled(!room.localParticipant.isScreenShareEnabled);
}));
audioUnlock.addEventListener('click', () => runMediaAction(async () => {
  await room.startAudio();
  audioUnlock.hidden = true;
  status.textContent = '远端音频已启用';
}));
leave.addEventListener('click', async () => {
  await room.disconnect();
  window.close();
});

async function join() {
  if (!serverUrl || !participantToken) throw new Error('会议加入凭据缺失或已失效');
  // Chromium 只在安全上下文暴露 mediaDevices；先给出可操作的安全错误，禁止抛出未定义属性异常。
  if (!window.isSecureContext || !navigator.mediaDevices?.getUserMedia)
    throw new Error('会议服务未通过 HTTPS 安全上下文加载，请联系管理员检查会议地址和证书。');
  await room.connect(serverUrl, participantToken);
  roomName.textContent = room.name || 'OrgLink 会议';
  status.textContent = '安全媒体连接已建立';
  ensureTile(room.localParticipant);
  mediaReady = true;
  await room.localParticipant.setMicrophoneEnabled(true);
  if (initialVideo) await room.localParticipant.setCameraEnabled(true);
  refreshControls();
  refreshAudioPlayback();
}

join().catch((error) => {
  status.textContent = '加入失败';
  const message = document.createElement('div');
  message.className = 'error';
  message.textContent = error instanceof Error ? error.message : '无法加入会议，请返回客户端重试。';
  participants.replaceChildren(message);
});
