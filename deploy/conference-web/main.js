import { Room, RoomEvent, Track } from 'livekit-client';

const participants = document.querySelector('#participants');
const status = document.querySelector('#status');
const roomName = document.querySelector('#room-name');
const microphone = document.querySelector('#microphone');
const camera = document.querySelector('#camera');
const screen = document.querySelector('#screen');
const leave = document.querySelector('#leave');

// JWT 只从 fragment 读取，随后立即清除地址栏，避免令牌进入代理日志、浏览器历史或复制链接。
const parameters = new URLSearchParams(location.hash.slice(1));
const serverUrl = parameters.get('server');
const participantToken = parameters.get('token');
const initialVideo = parameters.get('video') === '1';
history.replaceState(null, '', location.pathname);

const room = new Room({ adaptiveStream: true, dynacast: true });
const tiles = new Map();

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
  });

microphone.addEventListener('click', async () => {
  await room.localParticipant.setMicrophoneEnabled(!room.localParticipant.isMicrophoneEnabled);
  refreshControls();
});
camera.addEventListener('click', async () => {
  await room.localParticipant.setCameraEnabled(!room.localParticipant.isCameraEnabled);
  refreshControls();
});
screen.addEventListener('click', async () => {
  await room.localParticipant.setScreenShareEnabled(!room.localParticipant.isScreenShareEnabled);
  refreshControls();
});
leave.addEventListener('click', async () => {
  await room.disconnect();
  window.close();
});

async function join() {
  if (!serverUrl || !participantToken) throw new Error('会议加入凭据缺失或已失效');
  await room.connect(serverUrl, participantToken);
  roomName.textContent = room.name || 'OrgLink 会议';
  status.textContent = '安全媒体连接已建立';
  ensureTile(room.localParticipant);
  await room.localParticipant.setMicrophoneEnabled(true);
  if (initialVideo) await room.localParticipant.setCameraEnabled(true);
  refreshControls();
}

join().catch((error) => {
  status.textContent = '加入失败';
  const message = document.createElement('div');
  message.className = 'error';
  message.textContent = error instanceof Error ? error.message : '无法加入会议，请返回客户端重试。';
  participants.replaceChildren(message);
});
