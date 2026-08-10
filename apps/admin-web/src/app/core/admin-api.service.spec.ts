import { provideHttpClient } from '@angular/common/http';
import { HttpTestingController, provideHttpClientTesting } from '@angular/common/http/testing';
import { TestBed } from '@angular/core/testing';
import { AdminApiService } from './admin-api.service';

describe('AdminApiService', () => {
  let service: AdminApiService;
  let http: HttpTestingController;

  beforeEach(() => {
    TestBed.configureTestingModule({ providers: [provideHttpClient(), provideHttpClientTesting()] });
    service = TestBed.inject(AdminApiService);
    http = TestBed.inject(HttpTestingController);
  });

  afterEach(() => http.verify());

  it('should keep organization filters on the server-side person query', () => {
    service.persons('42', '张', 2, 20).subscribe();
    const request = http.expectOne((candidate) => candidate.url === '/api/admin/persons');
    expect(request.request.params.get('departmentId')).toBe('42');
    expect(request.request.params.get('search')).toBe('张');
    expect(request.request.params.get('page')).toBe('2');
    request.flush({ items: [], total: 0, page: 2, pageSize: 20 });
  });

  it('should call the dedicated share-revocation endpoint', () => {
    const uuid = '11111111-1111-1111-1111-111111111111';
    service.revokeFileShares(uuid).subscribe();
    const request = http.expectOne(`/api/admin/files/${uuid}/revoke-shares`);
    expect(request.request.method).toBe('POST');
    request.flush({ success: true });
  });
});
